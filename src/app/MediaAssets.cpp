#include "app/MediaAssets.h"

#include "core/Log.h"

namespace up {

namespace {

const char* kindName(MediaAssets::Kind kind) {
    return kind == MediaAssets::Kind::Thumbnail ? "thumbnail" : "waveform";
}

std::string memoryKey(const MediaItem& item, MediaAssets::Kind kind) {
    return std::string(kindName(kind)) + "|" + item.id + "|" + item.path.generic_string();
}

bool applicable(const MediaItem& item, MediaAssets::Kind kind) {
    if (!item.online) return false;
    return kind == MediaAssets::Kind::Thumbnail ? item.info.hasVideo : item.info.hasAudio;
}

}  // namespace

MediaAssets::MediaAssets(std::filesystem::path cacheDirectory, int workers, uint64_t maxCacheBytes)
    : cache_(std::move(cacheDirectory), maxCacheBytes), jobs_(workers) {}

MediaAssets::~MediaAssets() { jobs_.cancelAll(); }

void MediaAssets::setListener(Listener listener) {
    std::lock_guard lock(mutex_);
    listener_ = std::move(listener);
}

std::shared_ptr<const VideoFrame> MediaAssets::thumbnail(const MediaItem& item) {
    if (!applicable(item, Kind::Thumbnail)) return nullptr;
    return std::static_pointer_cast<const VideoFrame>(lookup(item, Kind::Thumbnail));
}

std::shared_ptr<const media::WaveformPeaks> MediaAssets::waveform(const MediaItem& item) {
    if (!applicable(item, Kind::Waveform)) return nullptr;
    return std::static_pointer_cast<const media::WaveformPeaks>(lookup(item, Kind::Waveform));
}

std::shared_ptr<const void> MediaAssets::lookup(const MediaItem& item, Kind kind) {
    const std::string key = memoryKey(item, kind);
    std::lock_guard lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        if (it->second.state != State::Ready) return nullptr;
        if (kind == Kind::Thumbnail) return it->second.thumbnail;
        return it->second.waveform;
    }
    entries_[key] = Entry{};
    // Thumbnails are cheap and visible in the media pool: run them before waveforms.
    const int priority = kind == Kind::Thumbnail ? 10 : 5;
    jobs_.submit(std::string(kindName(kind)) + " " + item.name, priority,
                 [this, key, item, kind](const CancelToken& cancel) { return generate(key, item, kind, cancel); });
    return nullptr;
}

Status MediaAssets::generate(const std::string& key, const MediaItem& item, Kind kind, const CancelToken& cancel) {
    auto finishFailed = [&](const Error& e) {
        std::lock_guard lock(mutex_);
        if (e.code == ErrorCode::Cancelled) entries_.erase(key);  // allow a later retry
        else if (auto it = entries_.find(key); it != entries_.end()) it->second.state = State::Failed;
        return Status(e);
    };

    auto fingerprint = media::fileFingerprint(item.path);
    if (!fingerprint.ok()) return finishFailed(fingerprint.error());
    const std::string diskKey = std::string(kindName(kind)) + "|v1|" + fingerprint.value() + "|" +
                                (kind == Kind::Thumbnail ? std::to_string(kThumbnailWidth) + "x" + std::to_string(kThumbnailHeight)
                                                         : std::string("48000/480"));
    const std::string ext = kind == Kind::Thumbnail ? ".ppm" : ".upwf";

    std::shared_ptr<const VideoFrame> thumb;
    std::shared_ptr<const media::WaveformPeaks> wave;
    if (auto bytes = cache_.read(diskKey, ext)) {
        if (kind == Kind::Thumbnail) {
            if (auto decoded = media::decodePpm(*bytes); decoded.ok())
                thumb = std::make_shared<const VideoFrame>(std::move(decoded.value()));
        } else if (auto decoded = media::decodeWaveform(*bytes); decoded.ok()) {
            wave = std::make_shared<const media::WaveformPeaks>(std::move(decoded.value()));
        }
        if (!thumb && !wave) UP_LOG_WARN("cache", "Discarding damaged cache entry for " << item.name);
    }
    if (!thumb && !wave) {
        std::string encoded;
        if (kind == Kind::Thumbnail) {
            auto generated = media::generateThumbnail(item.path, item.info, kThumbnailWidth, kThumbnailHeight, &cancel);
            if (!generated.ok()) return finishFailed(generated.error());
            encoded = media::encodePpm(generated.value());
            thumb = std::make_shared<const VideoFrame>(std::move(generated.value()));
        } else {
            auto generated = media::generateWaveform(item.path, item.info, 48000, 480, &cancel);
            if (!generated.ok()) return finishFailed(generated.error());
            encoded = media::encodeWaveform(generated.value());
            wave = std::make_shared<const media::WaveformPeaks>(std::move(generated.value()));
        }
        // A cache that cannot be written only costs regeneration later; the asset is still usable now.
        if (Status s = cache_.write(diskKey, ext, encoded); !s.ok()) UP_LOG_WARN("cache", s.error().message);
    }

    Listener listener;
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end()) return Status::success();  // reset() while running
        it->second.state = State::Ready;
        it->second.thumbnail = thumb;
        it->second.waveform = wave;
        listener = listener_;
    }
    if (listener) listener(item.id, kind);
    return Status::success();
}

void MediaAssets::prefetch(const Project& project) {
    for (const auto& m : project.media) {
        (void)thumbnail(m);
        (void)waveform(m);
    }
}

void MediaAssets::waitIdle() { jobs_.waitIdle(); }

void MediaAssets::reset() {
    jobs_.cancelAll();
    jobs_.waitIdle();
    std::lock_guard lock(mutex_);
    entries_.clear();
}

Status MediaAssets::clearCache() {
    reset();
    return cache_.clear();
}

std::size_t MediaAssets::failedCount() const {
    std::lock_guard lock(mutex_);
    std::size_t n = 0;
    for (const auto& [key, e] : entries_) n += e.state == State::Failed ? 1 : 0;
    return n;
}

}  // namespace up
