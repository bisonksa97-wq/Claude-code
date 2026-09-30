#include "render/Lut.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "core/AtomicFile.h"

namespace up::render {
namespace {

constexpr int kMax3DSize = 128;
constexpr int kMax1DSize = 65536;

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::vector<std::string> tokens(std::string_view line) {
    std::vector<std::string> out;
    std::istringstream in{std::string(line)};
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

bool toFloat(const std::string& s, float& out) {
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && std::isfinite(out);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

std::array<float, 3> Lut::apply(float r, float g, float b) const {
    std::array<float, 3> in{r, g, b};
    for (int c = 0; c < 3; ++c) {
        const float span = domainMax[c] - domainMin[c];
        in[c] = std::clamp((in[c] - domainMin[c]) / span, 0.0f, 1.0f) * static_cast<float>(size - 1);
    }
    if (!is3D) {
        std::array<float, 3> out{};
        for (int c = 0; c < 3; ++c) {
            const int i0 = std::min(static_cast<int>(in[c]), size - 2);
            const float f = in[c] - static_cast<float>(i0);
            out[c] = lerp(table[static_cast<std::size_t>(i0)][c], table[static_cast<std::size_t>(i0 + 1)][c], f);
        }
        return out;
    }
    const int r0 = std::min(static_cast<int>(in[0]), size - 2);
    const int g0 = std::min(static_cast<int>(in[1]), size - 2);
    const int b0 = std::min(static_cast<int>(in[2]), size - 2);
    const float fr = in[0] - static_cast<float>(r0);
    const float fg = in[1] - static_cast<float>(g0);
    const float fb = in[2] - static_cast<float>(b0);
    auto at = [&](int dr, int dg, int db) -> const std::array<float, 3>& {
        return table[static_cast<std::size_t>((r0 + dr) + size * ((g0 + dg) + size * (b0 + db)))];
    };
    const auto& c000 = at(0, 0, 0);
    const auto& c111 = at(1, 1, 1);
    // Tetrahedral interpolation: pick the tetrahedron of the cube containing the point
    // by ordering the fractional parts, then walk its three edges.
    const std::array<float, 3>* p1;
    const std::array<float, 3>* p2;
    float w1, w2, w3;  // weights along c000->p1, p1->p2, p2->c111
    if (fr > fg) {
        if (fg > fb) { p1 = &at(1, 0, 0); p2 = &at(1, 1, 0); w1 = fr; w2 = fg; w3 = fb; }
        else if (fr > fb) { p1 = &at(1, 0, 0); p2 = &at(1, 0, 1); w1 = fr; w2 = fb; w3 = fg; }
        else { p1 = &at(0, 0, 1); p2 = &at(1, 0, 1); w1 = fb; w2 = fr; w3 = fg; }
    } else {
        if (fb > fg) { p1 = &at(0, 0, 1); p2 = &at(0, 1, 1); w1 = fb; w2 = fg; w3 = fr; }
        else if (fb > fr) { p1 = &at(0, 1, 0); p2 = &at(0, 1, 1); w1 = fg; w2 = fb; w3 = fr; }
        else { p1 = &at(0, 1, 0); p2 = &at(1, 1, 0); w1 = fg; w2 = fr; w3 = fb; }
    }
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c)
        out[c] = c000[c] + w1 * ((*p1)[c] - c000[c]) + w2 * ((*p2)[c] - (*p1)[c]) + w3 * (c111[c] - (*p2)[c]);
    return out;
}

Result<Lut> parseCubeLut(std::string_view text, const std::string& sourceName) {
    Lut lut;
    int size3D = 0;
    int size1D = 0;
    int lineNo = 0;
    auto fail = [&](const std::string& what) -> Error {
        return makeError(ErrorCode::ParseError, "color", sourceName + " is not a valid .cube LUT: " + what,
                         "Check that the file is a 1D or 3D .cube LUT exported by a grading application.",
                         "line " + std::to_string(lineNo));
    };
    std::size_t expected = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        ++lineNo;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        if (std::isalpha(static_cast<unsigned char>(line.front()))) {
            if (!lut.table.empty()) return fail("keyword after the table data");
            const auto t = tokens(line);
            const std::string& key = t[0];
            if (key == "TITLE") {
                const auto q1 = line.find('"');
                const auto q2 = line.rfind('"');
                lut.title = q1 != q2 ? std::string(line.substr(q1 + 1, q2 - q1 - 1)) : std::string(trim(line.substr(5)));
            } else if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
                const bool is3D = key == "LUT_3D_SIZE";
                const int max = is3D ? kMax3DSize : kMax1DSize;
                const int n = t.size() == 2 ? std::atoi(t[1].c_str()) : 0;
                if (n < 2 || n > max) return fail(key + " must be between 2 and " + std::to_string(max));
                (is3D ? size3D : size1D) = n;
            } else if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
                auto& target = key == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax;
                if (t.size() != 4) return fail(key + " needs three numbers");
                for (int c = 0; c < 3; ++c)
                    if (!toFloat(t[static_cast<std::size_t>(c) + 1], target[static_cast<std::size_t>(c)])) return fail(key + " needs three numbers");
            } else if (key == "LUT_3D_INPUT_RANGE" || key == "LUT_1D_INPUT_RANGE") {
                float lo = 0, hi = 0;
                if (t.size() != 3 || !toFloat(t[1], lo) || !toFloat(t[2], hi)) return fail(key + " needs two numbers");
                lut.domainMin = {lo, lo, lo};
                lut.domainMax = {hi, hi, hi};
            }
            // Other keywords (vendor extensions) are ignored.
            continue;
        }
        if (lut.table.empty()) {
            if (size3D && size1D) return fail("it declares both LUT_1D_SIZE and LUT_3D_SIZE");
            if (!size3D && !size1D) return fail("table data before LUT_3D_SIZE or LUT_1D_SIZE");
            lut.is3D = size3D != 0;
            lut.size = lut.is3D ? size3D : size1D;
            expected = lut.is3D ? static_cast<std::size_t>(lut.size) * lut.size * lut.size : static_cast<std::size_t>(lut.size);
            lut.table.reserve(expected);
        }
        const auto t = tokens(line);
        std::array<float, 3> rgb{};
        if (t.size() != 3 || !toFloat(t[0], rgb[0]) || !toFloat(t[1], rgb[1]) || !toFloat(t[2], rgb[2]))
            return fail("expected three numbers per table line");
        if (lut.table.size() == expected) return fail("more table entries than the declared size");
        lut.table.push_back(rgb);
    }
    if (lut.table.empty()) {
        lineNo = 0;
        return fail("it contains no table data");
    }
    if (lut.table.size() != expected) {
        return fail("it has " + std::to_string(lut.table.size()) + " table entries but the size needs " +
                    std::to_string(expected));
    }
    for (int c = 0; c < 3; ++c)
        if (!(lut.domainMax[static_cast<std::size_t>(c)] > lut.domainMin[static_cast<std::size_t>(c)]))
            return fail("DOMAIN_MAX must be greater than DOMAIN_MIN");
    return lut;
}

Result<Lut> loadCubeLut(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".cube") {
        return makeError(ErrorCode::InvalidArgument, "color", "'" + path.filename().string() + "' is not a .cube LUT.",
                         "Only .cube LUT files are supported.");
    }
    auto text = readFile(path);
    if (!text.ok()) {
        return makeError(ErrorCode::NotFound, "color", "The LUT '" + path.filename().string() + "' could not be read.",
                         "Relink the LUT to its new location, or remove it from the grade.", path.string());
    }
    return parseCubeLut(text.value(), "'" + path.filename().string() + "'");
}

Result<std::shared_ptr<const Lut>> LutCache::get(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto modified = ec ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, ec);
    if (!ec) {
        auto it = entries_.find(path);
        if (it != entries_.end() && it->second.size == size && it->second.modified == modified) return it->second.lut;
    }
    auto lut = loadCubeLut(path);
    if (!lut.ok()) {
        entries_.erase(path);
        return lut.error();
    }
    auto shared = std::make_shared<const Lut>(std::move(lut.value()));
    entries_[path] = Entry{size, modified, shared};
    return shared;
}

}  // namespace up::render
