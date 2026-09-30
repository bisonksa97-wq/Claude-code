#include "render/ColorCurves.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace up::render {
namespace {

// Vectorscope angle of pure red (Rec.709): Cb = (0 - 0.2126) / 1.8556, Cr = (1 - 0.2126) / 1.5748 = 0.5.
const double kRedAngle = std::atan2(0.5, -0.2126 / 1.8556);

}  // namespace

double hueOf(double cb, double cr) {
    double turns = (std::atan2(cr, cb) - kRedAngle) / (2.0 * std::numbers::pi);
    turns -= std::floor(turns);
    return turns;
}

CurveEvaluator::CurveEvaluator(CurveKind kind, const std::vector<CurvePoint>& points) {
    periodic_ = isHueCurve(kind);
    tone_ = isToneCurve(kind);
    identity_ = points.empty();
    if (identity_) return;
    // Periodic curves get two wrapped copies of their points on each side, so the
    // spline is smooth across hue 0/1 and evaluation only ever happens inside.
    if (periodic_) {
        const std::size_t n = points.size();
        for (int side = -1; side <= 1; ++side) {
            for (std::size_t i = 0; i < n; ++i) {
                xs_.push_back(points[i].x + side);
                ys_.push_back(points[i].y);
            }
        }
    } else {
        for (const auto& p : points) {
            xs_.push_back(p.x);
            ys_.push_back(p.y);
        }
    }
    const std::size_t n = xs_.size();
    tangents_.assign(n, 0.0);
    if (n == 1) return;
    std::vector<double> secant(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) secant[i] = (ys_[i + 1] - ys_[i]) / (xs_[i + 1] - xs_[i]);
    tangents_[0] = secant[0];
    tangents_[n - 1] = secant[n - 2];
    for (std::size_t i = 1; i + 1 < n; ++i)
        tangents_[i] = secant[i - 1] * secant[i] <= 0.0 ? 0.0 : (secant[i - 1] + secant[i]) / 2.0;
    // Fritsch-Carlson: limit tangents so each segment stays monotone.
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (secant[i] == 0.0) {
            tangents_[i] = tangents_[i + 1] = 0.0;
            continue;
        }
        const double a = tangents_[i] / secant[i];
        const double b = tangents_[i + 1] / secant[i];
        if (a < 0.0) tangents_[i] = 0.0;
        if (b < 0.0) tangents_[i + 1] = 0.0;
        const double h = a * a + b * b;
        if (h > 9.0) {
            const double t = 3.0 / std::sqrt(h);
            tangents_[i] = t * a * secant[i];
            tangents_[i + 1] = t * b * secant[i];
        }
    }
}

double CurveEvaluator::operator()(double x) const {
    if (identity_) return tone_ ? std::clamp(x, 0.0, 1.0) : 0.5;
    if (periodic_) x -= std::floor(x);
    else x = std::clamp(x, 0.0, 1.0);
    if (xs_.size() == 1) return ys_[0];
    if (x <= xs_.front()) return ys_.front();
    if (x >= xs_.back()) return ys_.back();
    const auto it = std::upper_bound(xs_.begin(), xs_.end(), x);
    const std::size_t i = static_cast<std::size_t>(it - xs_.begin()) - 1;
    const double h = xs_[i + 1] - xs_[i];
    const double t = (x - xs_[i]) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double y = (2 * t3 - 3 * t2 + 1) * ys_[i] + (t3 - 2 * t2 + t) * h * tangents_[i] +
                     (-2 * t3 + 3 * t2) * ys_[i + 1] + (t3 - t2) * h * tangents_[i + 1];
    return std::clamp(y, 0.0, 1.0);
}

}  // namespace up::render
