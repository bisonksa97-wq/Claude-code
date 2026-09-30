#pragma once

#include <optional>
#include <string>
#include <vector>

namespace up {

// A colour space = RGB primaries (with a D65 white point) + a transfer function.
//
// Linear light is normalised so that 1.0 is reference (diffuse) white for display
// encodings: SDR white for sRGB/BT.1886/gamma 2.2, 203 cd/m² for PQ and the 75 %
// signal level for HLG (ITU-R BT.2408). Camera log encodings decode to scene-linear
// values with 18 % grey at 0.18.
enum class Primaries { Rec709, Rec2020, P3D65, ArriWideGamut3, SGamut3Cine };
enum class Transfer { Linear, SRGB, BT1886, Gamma22, PQ, HLG, LogC3, SLog3 };

struct ColorSpace {
    Primaries primaries = Primaries::Rec709;
    Transfer transfer = Transfer::BT1886;

    bool operator==(const ColorSpace&) const = default;
    // "rec709/bt1886" etc.
    std::string id() const;
    // "Rec.709 (gamma 2.4)" etc.; presets have short names, others list both parts.
    std::string displayName() const;
    static std::optional<ColorSpace> fromId(const std::string& id);
};

const char* primariesId(Primaries p);
const char* transferId(Transfer t);
const char* primariesName(Primaries p);
const char* transferName(Transfer t);
bool isHdr(Transfer t);  // PQ, HLG
bool isLog(Transfer t);  // LogC3, S-Log3

// The spaces offered in menus, in display order.
struct ColorSpacePreset {
    const char* name;
    ColorSpace space;
};
const std::vector<ColorSpacePreset>& colorSpacePresets();

// The space of media from its container tags (FFmpeg names such as "bt709",
// "smpte2084", "arib-std-b67", "iec61966-2-1"). Untagged video is assumed to be
// Rec.709 with a BT.1886 (gamma 2.4) display, untagged stills sRGB.
ColorSpace detectColorSpace(const std::string& primariesTag, const std::string& transferTag, bool still);

// FFmpeg tag names to write for a space (empty = leave unspecified, e.g. camera log).
struct ColorTags {
    std::string primaries;
    std::string transfer;
    std::string matrix;  // YUV matrix coefficients
};
ColorTags colorTagsFor(const ColorSpace& space);

}  // namespace up
