#include "core/Log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>

namespace up::log {
namespace {

struct State {
    std::mutex mutex;
    Level defaultLevel = Level::Info;
    std::map<std::string, Level, std::less<>> levels;
    Sink sink;
    std::ofstream file;
};

State& state() {
    static State s;
    return s;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

}  // namespace

const char* toString(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warning: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Off: return "OFF";
    }
    return "?";
}

void setDefaultLevel(Level level) {
    std::lock_guard lock(state().mutex);
    state().defaultLevel = level;
}

void setLevel(std::string_view subsystem, Level level) {
    std::lock_guard lock(state().mutex);
    state().levels[std::string(subsystem)] = level;
}

bool enabled(Level level, std::string_view subsystem) {
    std::lock_guard lock(state().mutex);
    const auto it = state().levels.find(subsystem);
    const Level threshold = it != state().levels.end() ? it->second : state().defaultLevel;
    return level != Level::Off && level >= threshold;
}

void write(Level level, std::string_view subsystem, std::string_view message) {
    std::lock_guard lock(state().mutex);
    auto& s = state();
    if (s.sink) {
        s.sink(level, subsystem, message);
    } else {
        std::cerr << '[' << toString(level) << "] [" << subsystem << "] " << message << '\n';
    }
    if (s.file.is_open()) {
        s.file << timestamp() << " [" << toString(level) << "] [" << subsystem << "] " << message << '\n';
        s.file.flush();
    }
}

void setSink(Sink sink) {
    std::lock_guard lock(state().mutex);
    state().sink = std::move(sink);
}

bool openLogFile(const std::filesystem::path& path) {
    std::lock_guard lock(state().mutex);
    state().file.close();
    state().file.open(path, std::ios::app);
    return state().file.is_open();
}

void closeLogFile() {
    std::lock_guard lock(state().mutex);
    state().file.close();
}

}  // namespace up::log
