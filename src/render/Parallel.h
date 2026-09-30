#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace up::render {

// Runs `fn(firstRow, endRow)` over [0, rows) in contiguous chunks on up to
// hardware_concurrency threads; the calling thread processes one chunk itself.
// Jobs smaller than about 64k pixel-operations run inline, so small previews and
// tests pay no threading cost. `fn` must only write to its own rows.
template <typename Fn>
void parallelRows(int rows, std::size_t workPerRow, Fn&& fn) {
    const std::size_t total = static_cast<std::size_t>(std::max(0, rows)) * workPerRow;
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const int chunks = static_cast<int>(std::min<std::size_t>({hardware, static_cast<std::size_t>(std::max(1, rows)), total / 65536 + 1}));
    if (chunks <= 1) {
        fn(0, rows);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(chunks - 1));
    const int per = (rows + chunks - 1) / chunks;
    for (int c = 1; c < chunks; ++c) {
        const int begin = c * per;
        const int end = std::min(rows, begin + per);
        if (begin >= end) break;
        workers.emplace_back([&fn, begin, end] { fn(begin, end); });
    }
    fn(0, std::min(rows, per));
    for (auto& w : workers) w.join();
}

}  // namespace up::render
