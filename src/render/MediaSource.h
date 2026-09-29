#pragma once

#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "codec/AudioDecoder.h"
#include "codec/VideoDecoder.h"
#include "core/Result.h"
#include "project/Project.h"

namespace up::render {

// Resolves a clip's mediaId to the file to decode. Returns nullptr when unknown.
using MediaResolver = std::function<const MediaItem*(const std::string& mediaId)>;

MediaResolver resolverFor(const Project& project);

// Bounded LRU cache of open decoders. Decoders are keyed by clip so that two
// clips reading the same file at different positions do not thrash one decoder.
class DecoderPool {
public:
    explicit DecoderPool(std::size_t capacity = 16) : capacity_(capacity) {}

    Result<VideoDecoder*> video(const std::string& key, const std::filesystem::path& path);
    Result<AudioDecoder*> audio(const std::string& key, const std::filesystem::path& path, int sampleRate,
                                int channels);
    void clear();

private:
    template <typename T>
    struct Entry {
        std::string key;
        std::filesystem::path path;
        std::unique_ptr<T> decoder;
    };
    std::size_t capacity_;
    std::list<Entry<VideoDecoder>> video_;
    std::list<Entry<AudioDecoder>> audio_;
};

}  // namespace up::render
