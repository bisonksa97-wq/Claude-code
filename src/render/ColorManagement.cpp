#include "render/ColorManagement.h"

#include <cmath>

#include "render/Parallel.h"

namespace up::render {
namespace {

// --- Transfer functions ---------------------------------------------------------------

double mirroredPow(double v, double exponent) { return v < 0.0 ? -std::pow(-v, exponent) : std::pow(v, exponent); }

// ST 2084 (PQ) constants.
constexpr double kPqM1 = 2610.0 / 16384.0;
constexpr double kPqM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kPqC1 = 3424.0 / 4096.0;
constexpr double kPqC2 = 2413.0 / 4096.0 * 32.0;
constexpr double kPqC3 = 2392.0 / 4096.0 * 32.0;
constexpr double kReferenceWhiteNits = 203.0;  // BT.2408 HDR reference white

// BT.2100 HLG constants.
constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892;  // 1 - 4a
constexpr double kHlgC = 0.55991073;  // 0.5 - a ln(4a)

double hlgInverseOetf(double e) {
    if (e <= 0.5) return e * e / 3.0;
    return (std::exp((e - kHlgC) / kHlgA) + kHlgB) / 12.0;
}

double hlgOetf(double e) {
    if (e <= 1.0 / 12.0) return std::sqrt(3.0 * std::max(0.0, e));
    return kHlgA * std::log(12.0 * e - kHlgB) + kHlgC;
}

// Scene light of the 75 % HLG signal: reference white, mapped to linear 1.0.
const double kHlgReference = hlgInverseOetf(0.75);

// ARRI LogC3 (EI 800).
constexpr double kLogCCut = 0.010591, kLogCA = 5.555556, kLogCB = 0.052272, kLogCC = 0.247190, kLogCD = 0.385537,
                 kLogCE = 5.367655, kLogCF = 0.092809;

// Sony S-Log3.
constexpr double kSLog3Break = 171.2102946929 / 1023.0;

}  // namespace

double decodeTransfer(Transfer t, double v) {
    switch (t) {
        case Transfer::Linear: return v;
        case Transfer::SRGB: {
            const double a = std::abs(v);
            const double l = a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4);
            return v < 0 ? -l : l;
        }
        case Transfer::BT1886: return mirroredPow(v, 2.4);
        case Transfer::Gamma22: return mirroredPow(v, 2.2);
        case Transfer::PQ: {
            const double p = std::pow(std::max(v, 0.0), 1.0 / kPqM2);
            const double y = std::pow(std::max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), 1.0 / kPqM1);
            return y * 10000.0 / kReferenceWhiteNits;
        }
        case Transfer::HLG: return hlgInverseOetf(std::max(v, 0.0)) / kHlgReference;
        case Transfer::LogC3:
            return v > kLogCE * kLogCCut + kLogCF ? (std::pow(10.0, (v - kLogCD) / kLogCC) - kLogCB) / kLogCA
                                                  : (v - kLogCF) / kLogCE;
        case Transfer::SLog3:
            return v >= kSLog3Break ? std::pow(10.0, (v * 1023.0 - 420.0) / 261.5) * (0.18 + 0.01) - 0.01
                                    : (v * 1023.0 - 95.0) * 0.01125 / (171.2102946929 - 95.0);
    }
    return v;
}

double encodeTransfer(Transfer t, double l) {
    switch (t) {
        case Transfer::Linear: return l;
        case Transfer::SRGB: {
            const double a = std::abs(l);
            const double e = a <= 0.0031308 ? a * 12.92 : 1.055 * std::pow(a, 1.0 / 2.4) - 0.055;
            return l < 0 ? -e : e;
        }
        case Transfer::BT1886: return mirroredPow(l, 1.0 / 2.4);
        case Transfer::Gamma22: return mirroredPow(l, 1.0 / 2.2);
        case Transfer::PQ: {
            const double y = std::pow(std::max(l, 0.0) * kReferenceWhiteNits / 10000.0, kPqM1);
            return std::pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), kPqM2);
        }
        case Transfer::HLG: return hlgOetf(std::max(l, 0.0) * kHlgReference);
        case Transfer::LogC3:
            return l > kLogCCut ? kLogCC * std::log10(kLogCA * l + kLogCB) + kLogCD : kLogCE * l + kLogCF;
        case Transfer::SLog3:
            return l >= 0.01125 ? (420.0 + std::log10((l + 0.01) / (0.18 + 0.01)) * 261.5) / 1023.0
                                : (l * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0;
    }
    return l;
}

// --- Primaries ----------------------------------------------------------------------------

namespace {

struct Chromaticities {
    double rx, ry, gx, gy, bx, by;
};

Chromaticities chromaticitiesOf(Primaries p) {
    switch (p) {
        case Primaries::Rec709: return {0.640, 0.330, 0.300, 0.600, 0.150, 0.060};
        case Primaries::Rec2020: return {0.708, 0.292, 0.170, 0.797, 0.131, 0.046};
        case Primaries::P3D65: return {0.680, 0.320, 0.265, 0.690, 0.150, 0.060};
        case Primaries::ArriWideGamut3: return {0.6840, 0.3130, 0.2210, 0.8480, 0.0861, -0.1020};
        case Primaries::SGamut3Cine: return {0.766, 0.275, 0.225, 0.800, 0.089, -0.087};
    }
    return {0.640, 0.330, 0.300, 0.600, 0.150, 0.060};
}

constexpr double kD65x = 0.3127, kD65y = 0.3290;

Matrix3 multiply(const Matrix3& a, const Matrix3& b) {
    Matrix3 m{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) m[i][j] += a[i][k] * b[k][j];
    return m;
}

Matrix3 inverse(const Matrix3& m) {
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    Matrix3 r{};
    r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return r;
}

}  // namespace

std::array<std::array<double, 2>, 4> primariesChromaticities(Primaries primaries) {
    const Chromaticities c = chromaticitiesOf(primaries);
    return {{{c.rx, c.ry}, {c.gx, c.gy}, {c.bx, c.by}, {kD65x, kD65y}}};
}

Matrix3 rgbToXyz(Primaries primaries) {
    const Chromaticities c = chromaticitiesOf(primaries);
    auto column = [](double x, double y) { return std::array<double, 3>{x / y, 1.0, (1.0 - x - y) / y}; };
    const auto r = column(c.rx, c.ry), g = column(c.gx, c.gy), b = column(c.bx, c.by);
    const Matrix3 p = {{{r[0], g[0], b[0]}, {r[1], g[1], b[1]}, {r[2], g[2], b[2]}}};
    const auto w = column(kD65x, kD65y);
    const Matrix3 pi = inverse(p);
    std::array<double, 3> s{};
    for (int i = 0; i < 3; ++i) s[i] = pi[i][0] * w[0] + pi[i][1] * w[1] + pi[i][2] * w[2];
    Matrix3 m{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i][j] = p[i][j] * s[j];
    return m;
}

Matrix3 primariesConversion(Primaries from, Primaries to) {
    return multiply(inverse(rgbToXyz(to)), rgbToXyz(from));
}

// --- Conversion ---------------------------------------------------------------------------

ColorConversion::ColorConversion(ColorSpace from, ColorSpace to) : from_(from), to_(to) {
    samePrimaries_ = from.primaries == to.primaries;
    sameTransfer_ = from.transfer == to.transfer;
    identity_ = samePrimaries_ && sameTransfer_;
    if (!samePrimaries_) matrix_ = primariesConversion(from.primaries, to.primaries);
}

std::array<float, 3> ColorConversion::fromLinear(double r, double g, double b) const {
    if (!samePrimaries_) {
        const double nr = matrix_[0][0] * r + matrix_[0][1] * g + matrix_[0][2] * b;
        const double ng = matrix_[1][0] * r + matrix_[1][1] * g + matrix_[1][2] * b;
        const double nb = matrix_[2][0] * r + matrix_[2][1] * g + matrix_[2][2] * b;
        r = nr;
        g = ng;
        b = nb;
    }
    return {static_cast<float>(encodeTransfer(to_.transfer, r)), static_cast<float>(encodeTransfer(to_.transfer, g)),
            static_cast<float>(encodeTransfer(to_.transfer, b))};
}

std::array<float, 3> ColorConversion::apply(float r, float g, float b) const {
    if (identity_) return {r, g, b};
    return fromLinear(decodeTransfer(from_.transfer, r), decodeTransfer(from_.transfer, g), decodeTransfer(from_.transfer, b));
}

void ColorConversion::apply(FloatFrame& frame) const {
    if (identity_) return;
    parallelRows(frame.height, static_cast<std::size_t>(frame.width) * 32, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* px = frame.row(y);
            for (int x = 0; x < frame.width; ++x, px += 4) {
                const auto out = apply(px[0], px[1], px[2]);
                px[0] = out[0];
                px[1] = out[1];
                px[2] = out[2];
            }
        }
    });
}

FloatFrame ColorConversion::convert(const VideoFrame16& frame) const {
    FloatFrame out = toFloatFrame(frame);
    apply(out);
    return out;
}

FloatFrame ColorConversion::convert(const VideoFrame& frame) const {
    FloatFrame out = toFloatFrame(frame);
    if (identity_) return out;
    std::array<double, 256> decoded{};
    for (int i = 0; i < 256; ++i) decoded[static_cast<std::size_t>(i)] = decodeTransfer(from_.transfer, i / 255.0);
    parallelRows(frame.height, static_cast<std::size_t>(frame.width) * 16, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uint8_t* src = frame.row(y);
            float* dst = out.row(y);
            for (int x = 0; x < frame.width; ++x, src += 4, dst += 4) {
                const auto rgb = fromLinear(decoded[src[0]], decoded[src[1]], decoded[src[2]]);
                dst[0] = rgb[0];
                dst[1] = rgb[1];
                dst[2] = rgb[2];
            }
        }
    });
    return out;
}

}  // namespace up::render
