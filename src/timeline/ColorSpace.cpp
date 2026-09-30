#include "timeline/ColorSpace.h"

#include <array>
#include <utility>

namespace up {
namespace {

struct PrimariesInfo {
    Primaries value;
    const char* id;
    const char* name;
};
constexpr std::array<PrimariesInfo, 5> kPrimaries = {{
    {Primaries::Rec709, "rec709", "Rec.709"},
    {Primaries::Rec2020, "rec2020", "Rec.2020"},
    {Primaries::P3D65, "p3d65", "P3-D65"},
    {Primaries::ArriWideGamut3, "awg3", "ARRI Wide Gamut 3"},
    {Primaries::SGamut3Cine, "sgamut3cine", "S-Gamut3.Cine"},
}};

struct TransferInfo {
    Transfer value;
    const char* id;
    const char* name;
};
constexpr std::array<TransferInfo, 8> kTransfers = {{
    {Transfer::Linear, "linear", "Linear"},
    {Transfer::SRGB, "srgb", "sRGB"},
    {Transfer::BT1886, "bt1886", "Gamma 2.4 (BT.1886)"},
    {Transfer::Gamma22, "gamma22", "Gamma 2.2"},
    {Transfer::PQ, "pq", "PQ (ST 2084)"},
    {Transfer::HLG, "hlg", "HLG"},
    {Transfer::LogC3, "logc3", "ARRI LogC3"},
    {Transfer::SLog3, "slog3", "Sony S-Log3"},
}};

}  // namespace

const char* primariesId(Primaries p) { return kPrimaries[static_cast<std::size_t>(p)].id; }
const char* primariesName(Primaries p) { return kPrimaries[static_cast<std::size_t>(p)].name; }
const char* transferId(Transfer t) { return kTransfers[static_cast<std::size_t>(t)].id; }
const char* transferName(Transfer t) { return kTransfers[static_cast<std::size_t>(t)].name; }
bool isHdr(Transfer t) { return t == Transfer::PQ || t == Transfer::HLG; }
bool isLog(Transfer t) { return t == Transfer::LogC3 || t == Transfer::SLog3; }

std::string ColorSpace::id() const { return std::string(primariesId(primaries)) + "/" + transferId(transfer); }

std::string ColorSpace::displayName() const {
    for (const auto& p : colorSpacePresets())
        if (p.space == *this) return p.name;
    return std::string(primariesName(primaries)) + " · " + transferName(transfer);
}

std::optional<ColorSpace> ColorSpace::fromId(const std::string& id) {
    const auto slash = id.find('/');
    if (slash == std::string::npos) {
        // Short aliases for the common presets.
        static const std::array<std::pair<const char*, ColorSpace>, 9> aliases = {{
            {"rec709", {Primaries::Rec709, Transfer::BT1886}},
            {"srgb", {Primaries::Rec709, Transfer::SRGB}},
            {"linear", {Primaries::Rec709, Transfer::Linear}},
            {"p3", {Primaries::P3D65, Transfer::SRGB}},
            {"rec2020", {Primaries::Rec2020, Transfer::BT1886}},
            {"pq", {Primaries::Rec2020, Transfer::PQ}},
            {"hlg", {Primaries::Rec2020, Transfer::HLG}},
            {"logc3", {Primaries::ArriWideGamut3, Transfer::LogC3}},
            {"slog3", {Primaries::SGamut3Cine, Transfer::SLog3}},
        }};
        for (const auto& [alias, space] : aliases)
            if (id == alias) return space;
        return std::nullopt;
    }
    const std::string pid = id.substr(0, slash);
    const std::string tid = id.substr(slash + 1);
    std::optional<Primaries> primaries;
    std::optional<Transfer> transfer;
    for (const auto& p : kPrimaries)
        if (pid == p.id) primaries = p.value;
    for (const auto& t : kTransfers)
        if (tid == t.id) transfer = t.value;
    if (!primaries || !transfer) return std::nullopt;
    return ColorSpace{*primaries, *transfer};
}

const std::vector<ColorSpacePreset>& colorSpacePresets() {
    static const std::vector<ColorSpacePreset> presets = {
        {"Rec.709 (gamma 2.4)", {Primaries::Rec709, Transfer::BT1886}},
        {"sRGB", {Primaries::Rec709, Transfer::SRGB}},
        {"Rec.709 (gamma 2.2)", {Primaries::Rec709, Transfer::Gamma22}},
        {"Linear Rec.709", {Primaries::Rec709, Transfer::Linear}},
        {"Display P3", {Primaries::P3D65, Transfer::SRGB}},
        {"Rec.2020 (gamma 2.4)", {Primaries::Rec2020, Transfer::BT1886}},
        {"Rec.2100 PQ", {Primaries::Rec2020, Transfer::PQ}},
        {"Rec.2100 HLG", {Primaries::Rec2020, Transfer::HLG}},
        {"Linear Rec.2020", {Primaries::Rec2020, Transfer::Linear}},
        {"ARRI LogC3 (AWG3)", {Primaries::ArriWideGamut3, Transfer::LogC3}},
        {"Sony S-Log3 (S-Gamut3.Cine)", {Primaries::SGamut3Cine, Transfer::SLog3}},
    };
    return presets;
}

ColorSpace detectColorSpace(const std::string& primariesTag, const std::string& transferTag, bool still) {
    ColorSpace s = still ? ColorSpace{Primaries::Rec709, Transfer::SRGB} : ColorSpace{};
    if (primariesTag == "bt2020") s.primaries = Primaries::Rec2020;
    else if (primariesTag == "smpte432") s.primaries = Primaries::P3D65;
    else if (primariesTag == "bt709") s.primaries = Primaries::Rec709;
    if (transferTag == "smpte2084") s.transfer = Transfer::PQ;
    else if (transferTag == "arib-std-b67") s.transfer = Transfer::HLG;
    else if (transferTag == "iec61966-2-1") s.transfer = Transfer::SRGB;
    else if (transferTag == "linear") s.transfer = Transfer::Linear;
    else if (transferTag == "gamma22") s.transfer = Transfer::Gamma22;
    // bt709 / smpte170m / bt2020-10 / bt2020-12 are camera OETFs displayed with BT.1886.
    else if (transferTag == "bt709" || transferTag == "smpte170m" || transferTag == "bt2020-10" || transferTag == "bt2020-12")
        s.transfer = Transfer::BT1886;
    return s;
}

ColorTags colorTagsFor(const ColorSpace& space) {
    ColorTags t;
    switch (space.primaries) {
        case Primaries::Rec709: t.primaries = "bt709"; t.matrix = "bt709"; break;
        case Primaries::Rec2020: t.primaries = "bt2020"; t.matrix = "bt2020nc"; break;
        case Primaries::P3D65: t.primaries = "smpte432"; t.matrix = "bt709"; break;
        default: t.matrix = "bt709"; break;  // camera gamuts have no standard tag
    }
    switch (space.transfer) {
        case Transfer::Linear: t.transfer = "linear"; break;
        case Transfer::SRGB: t.transfer = "iec61966-2-1"; break;
        case Transfer::BT1886: t.transfer = space.primaries == Primaries::Rec2020 ? "bt2020-10" : "bt709"; break;
        case Transfer::Gamma22: t.transfer = "gamma22"; break;
        case Transfer::PQ: t.transfer = "smpte2084"; break;
        case Transfer::HLG: t.transfer = "arib-std-b67"; break;
        default: break;
    }
    return t;
}

}  // namespace up
