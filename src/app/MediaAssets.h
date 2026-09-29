#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "cache/DiskCache.h"
#include "codec/VideoFrame.h"
#include "jobs/JobQueue.h"
#include "media/MediaAnalysis.h"
#include "project/Project.h"

namespace up {

// Provides derived media assets (thumbnails, waveform peaks) without blocking callers.
//
// Lookups return immediately: the in-memory result if it is ready, otherwise
// nullptr, after scheduling generation on the background JobQueue. Results are
// persisted in the DiskCache, keyed by the file's fingerprint (path, size, mtime),
// so a changed or relinked file gets fresh assets while unchanged files are never
// regenerated. The listener fires (on a worker thread) when an asset becomes ready.
class MediaAssets {
public:
    enum class Kind { Thumbnail, Waveform };
    using Listener = std::function<void(const std::string& mediaId, Kind kind)>;

    explicit MediaAssets(std::filesystem::path cacheDirectory, int workers = 2,
                         uint64_t maxCacheBytes = 2ull << 30);
    ~MediaAssets();

    MediaAssets(const MediaAssets&) = delete;
    MediaAssets& operator=(const MediaAssets&) = delete;

    void setListener(Listener listener);

    std::shared_ptr<const VideoFrame> thumbnail(const MediaItem& item);
    std::shared_ptr<const media::WaveformPeaks> waveform(const MediaItem& item);

    // Schedules every missing asset of the project's online media.
    void prefetch(const Project& project);
    // Blocks until all scheduled work has finished (CLI and tests).
    void waitIdle();
    // Cancels outstanding work and forgets in-memory results and failures.
    void reset();
    // Deletes the disk cache (in-memory results are also dropped).
    Status clearCache();

    DiskCache& cache() { return cache_; }
    std::size_t failedCount() const;

    static constexpr int kThumbnailWidth = 192;
    static constexpr int kThumbnailHeight = 108;

private:
    enum class State { Pending, Ready, Failed };
    struct Entry {
        State state = State::Pending;
        std::shared_ptr<const VideoFrame> thumbnail;
        std::shared_ptr<const media::WaveformPeaks> waveform;
    };

    std::shared_ptr<const void> lookup(const MediaItem& item, Kind kind);
    Status generate(const std::string& memoryKey, const MediaItem& item, Kind kind, const CancelToken& cancel);

    DiskCache cache_;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;  // key: kind|mediaId|path
    Listener listener_;
    // Declared last so it is destroyed first: workers must stop before the state above goes away.
    JobQueue jobs_;
};

}  // namespace up
