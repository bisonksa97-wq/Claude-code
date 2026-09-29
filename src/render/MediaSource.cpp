#include "render/MediaSource.h"

namespace up::render {

MediaResolver resolverFor(const Project& project) {
    return [&project](const std::string& mediaId) { return project.findMedia(mediaId); };
}

namespace {

template <typename T, typename OpenFn>
Result<T*> lookup(std::list<T>& cache, std::size_t capacity, const std::string& key,
                  const std::filesystem::path& path, OpenFn&& open) {
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->key == key && it->path == path) {
            cache.splice(cache.begin(), cache, it);
            return &cache.front();
        }
    }
    auto opened = open();
    if (!opened.ok()) return opened.error();
    cache.push_front(T{key, path, std::move(opened.value())});
    while (cache.size() > capacity) cache.pop_back();
    return &cache.front();
}

}  // namespace

Result<VideoDecoder*> DecoderPool::video(const std::string& key, const std::filesystem::path& path) {
    auto entry = lookup(video_, capacity_, key, path, [&] { return VideoDecoder::open(path); });
    if (!entry.ok()) return entry.error();
    return entry.value()->decoder.get();
}

Result<AudioDecoder*> DecoderPool::audio(const std::string& key, const std::filesystem::path& path, int sampleRate,
                                         int channels) {
    auto entry = lookup(audio_, capacity_, key, path, [&] { return AudioDecoder::open(path, sampleRate, channels); });
    if (!entry.ok()) return entry.error();
    return entry.value()->decoder.get();
}

void DecoderPool::clear() {
    video_.clear();
    audio_.clear();
}

}  // namespace up::render
