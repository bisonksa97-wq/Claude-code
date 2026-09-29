#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "core/Result.h"

namespace up {

// Content-addressed cache of derived data (thumbnails, waveforms, later proxies
// and renders). Everything in it can be regenerated, so the whole directory is
// safe to delete at any time. Entries are written atomically; the cache is
// trimmed to `maxBytes` by evicting least-recently-used entries.
class DiskCache {
public:
    explicit DiskCache(std::filesystem::path root, uint64_t maxBytes = 2ull << 30);

    const std::filesystem::path& root() const { return root_; }
    uint64_t maxBytes() const { return maxBytes_; }

    // Location of the entry for `key` (hashed) with extension `ext` (e.g. ".ppm").
    std::filesystem::path pathFor(std::string_view key, std::string_view ext) const;

    // Returns the entry's bytes, or nullopt when missing/unreadable. Marks it recently used.
    std::optional<std::string> read(std::string_view key, std::string_view ext) const;
    Status write(std::string_view key, std::string_view ext, std::string_view bytes);
    bool contains(std::string_view key, std::string_view ext) const;
    void remove(std::string_view key, std::string_view ext);

    uint64_t sizeBytes() const;
    std::size_t entryCount() const;
    // Evicts least-recently-used entries until the cache fits in maxBytes.
    void trim();
    Status clear();

    // 64-bit FNV-1a, rendered as 16 hex characters.
    static std::string hashKey(std::string_view key);
    // Per-user cache location (XDG_CACHE_HOME, LOCALAPPDATA, ~/Library/Caches, or temp).
    static std::filesystem::path defaultDirectory();

private:
    std::filesystem::path root_;
    uint64_t maxBytes_;
    mutable std::mutex trimMutex_;
};

}  // namespace up
