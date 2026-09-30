#pragma once

#include <vector>

#include "timeline/Grade.h"

namespace up::render {

// Evaluates a grade curve (see CurveKind in timeline/Grade.h) with monotone cubic
// Hermite interpolation (Fritsch-Carlson tangents): the curve passes through every
// point, is monotone wherever the points are, and never overshoots them.
class CurveEvaluator {
public:
    // Points must be valid (validateCurve). An empty point list is the identity for
    // tone curves and neutral (0.5) for the others.
    CurveEvaluator(CurveKind kind, const std::vector<CurvePoint>& points);

    bool isIdentity() const { return identity_; }
    // Input 0..1 (clamped for tone curves; wrapped for hue curves). Result 0..1.
    double operator()(double x) const;

private:
    bool identity_ = true;
    bool periodic_ = false;
    bool tone_ = true;  // empty tone curves are the identity, others are neutral (0.5)
    std::vector<double> xs_, ys_, tangents_;
};

// Hue angle used by the hue curves: 0..1 turns around the vectorscope, red = 0,
// increasing through yellow, green, cyan, blue and magenta.
double hueOf(double cb, double cr);

}  // namespace up::render
