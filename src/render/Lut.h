#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Result.h"

namespace up::render {

// A colour lookup table loaded from an Adobe/Resolve `.cube` file.
//
// 3D tables are sampled with tetrahedral interpolation, 1D tables linearly per
// channel. Input outside the domain is clamped to it; output is not clamped here.
struct Lut {
    std::string title;
    int size = 0;              // entries per axis
    bool is3D = true;
    std::array<float, 3> domainMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainMax{1.0f, 1.0f, 1.0f};
    // RGB triples. 3D: index = r + size * (g + size * b) (red changes fastest, as in
    // the file). 1D: one entry per step.
    std::vector<std::array<float, 3>> table;

    std::array<float, 3> apply(float r, float g, float b) const;
};

// Parses `.cube` text. Errors name the line and say what is wrong.
Result<Lut> parseCubeLut(std::string_view text, const std::string& sourceName = "LUT");
Result<Lut> loadCubeLut(const std::filesystem::path& path);

// Loaded LUTs by file, reloaded when the file's size or modification time change.
// Not thread-safe: each renderer owns one.
class LutCache {
public:
    Result<std::shared_ptr<const Lut>> get(const std::filesystem::path& path);

private:
    struct Entry {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified;
        std::shared_ptr<const Lut> lut;
    };
    std::map<std::filesystem::path, Entry> entries_;
};

}  // namespace up::render
