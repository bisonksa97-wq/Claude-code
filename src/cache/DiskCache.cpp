#include "cache/DiskCache.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "core/AtomicFile.h"
#include "core/Log.h"

namespace up {
namespace fs = std::filesystem;

namespace {
// Entries use this suffix so clear()/trim() never touch foreign files that happen to live in the folder.
constexpr std::string_view kSuffix = ".upc";
}  // namespace

DiskCache::DiskCache(fs::path root, uint64_t maxBytes) : root_(std::move(root)), maxBytes_(maxBytes) {}

std::string DiskCache::hashKey(std::string_view key) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ull;
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = hex[h & 0xF];
        h >>= 4;
    }
    return out;
}

fs::path DiskCache::defaultDirectory() {
    auto env = [](const char* name) -> std::string {
        const char* v = std::getenv(name);
        return v ? v : "";
    };
#ifdef _WIN32
    if (auto v = env("LOCALAPPDATA"); !v.empty()) return fs::path(v) / "UltimatePost" / "Cache";
#elif defined(__APPLE__)
    if (auto v = env("HOME"); !v.empty()) return fs::path(v) / "Library" / "Caches" / "UltimatePost";
#else
    if (auto v = env("XDG_CACHE_HOME"); !v.empty()) return fs::path(v) / "UltimatePost";
    if (auto v = env("HOME"); !v.empty()) return fs::path(v) / ".cache" / "UltimatePost";
#endif
    std::error_code ec;
    return fs::temp_directory_path(ec) / "UltimatePost" / "Cache";
}

fs::path DiskCache::pathFor(std::string_view key, std::string_view ext) const {
    const std::string hash = hashKey(key);
    return root_ / hash.substr(0, 2) / (hash + std::string(ext) + std::string(kSuffix));
}

std::optional<std::string> DiskCache::read(std::string_view key, std::string_view ext) const {
    const fs::path p = pathFor(key, ext);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return std::nullopt;
    auto bytes = readFile(p);
    if (!bytes.ok()) return std::nullopt;
    fs::last_write_time(p, fs::file_time_type::clock::now(), ec);  // LRU touch
    return std::move(bytes.value());
}

Status DiskCache::write(std::string_view key, std::string_view ext, std::string_view bytes) {
    const fs::path p = pathFor(key, ext);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
        return makeError(ErrorCode::IoError, "cache",
                         "Unable to create the cache folder '" + p.parent_path().string() + "'.",
                         "Check that the cache location is writable, or choose another one.", ec.message());
    }
    UP_TRY(writeFileAtomically(p, bytes, /*keepBackup=*/false));
    if (sizeBytes() > maxBytes_) trim();
    return Status::success();
}

bool DiskCache::contains(std::string_view key, std::string_view ext) const {
    std::error_code ec;
    return fs::is_regular_file(pathFor(key, ext), ec);
}

void DiskCache::remove(std::string_view key, std::string_view ext) {
    std::error_code ec;
    fs::remove(pathFor(key, ext), ec);
}

namespace {

struct EntryFile {
    fs::path path;
    uint64_t size;
    fs::file_time_type time;
};

std::vector<EntryFile> listEntries(const fs::path& root) {
    std::vector<EntryFile> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        if (name.size() < kSuffix.size() || name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
            continue;
        out.push_back({it->path(), static_cast<uint64_t>(it->file_size(ec)), it->last_write_time(ec)});
    }
    return out;
}

}  // namespace

uint64_t DiskCache::sizeBytes() const {
    uint64_t total = 0;
    for (const auto& e : listEntries(root_)) total += e.size;
    return total;
}

std::size_t DiskCache::entryCount() const { return listEntries(root_).size(); }

void DiskCache::trim() {
    std::lock_guard lock(trimMutex_);
    auto entries = listEntries(root_);
    uint64_t total = 0;
    for (const auto& e : entries) total += e.size;
    if (total <= maxBytes_) return;
    std::sort(entries.begin(), entries.end(), [](const EntryFile& a, const EntryFile& b) { return a.time < b.time; });
    std::size_t evicted = 0;
    for (const auto& e : entries) {
        if (total <= maxBytes_) break;
        std::error_code ec;
        if (fs::remove(e.path, ec)) {
            total -= e.size;
            ++evicted;
        }
    }
    UP_LOG_DEBUG("cache", "Evicted " << evicted << " cache entries; now " << total << " bytes");
}

Status DiskCache::clear() {
    std::lock_guard lock(trimMutex_);
    for (const auto& e : listEntries(root_)) {
        std::error_code ec;
        fs::remove(e.path, ec);
        if (ec) {
            return makeError(ErrorCode::IoError, "cache", "Unable to delete cache file '" + e.path.string() + "'.",
                             "Close programs that may be using it and try again.", ec.message());
        }
    }
    return Status::success();
}

}  // namespace up
