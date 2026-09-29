#include "core/Id.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <random>

namespace up {

std::string generateId() {
    static std::mutex mutex;
    static std::mt19937_64 engine{std::random_device{}()};
    uint64_t a = 0;
    uint64_t b = 0;
    {
        std::lock_guard lock(mutex);
        a = engine();
        b = engine();
    }
    static constexpr std::array<char, 16> hex{'0', '1', '2', '3', '4', '5', '6', '7',
                                              '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) {
        out[static_cast<std::size_t>(i)] = hex[(a >> (60 - 4 * i)) & 0xF];
        out[static_cast<std::size_t>(16 + i)] = hex[(b >> (60 - 4 * i)) & 0xF];
    }
    return out;
}

}  // namespace up
