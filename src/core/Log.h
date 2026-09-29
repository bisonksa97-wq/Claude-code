#pragma once

#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>

namespace up::log {

enum class Level { Trace = 0, Debug, Info, Warning, Error, Off };

const char* toString(Level level);

// Subsystem names used across the code base. Levels can be set per subsystem.
namespace sub {
inline constexpr const char* App = "app";
inline constexpr const char* Project = "project";
inline constexpr const char* Media = "media";
inline constexpr const char* Codec = "codec";
inline constexpr const char* Timeline = "timeline";
inline constexpr const char* Render = "render";
inline constexpr const char* Audio = "audio";
inline constexpr const char* Ui = "ui";
}  // namespace sub

using Sink = std::function<void(Level, std::string_view subsystem, std::string_view message)>;

void setDefaultLevel(Level level);
void setLevel(std::string_view subsystem, Level level);
bool enabled(Level level, std::string_view subsystem);
void write(Level level, std::string_view subsystem, std::string_view message);

// Replaces the default stderr sink. Pass nullptr to restore stderr output.
void setSink(Sink sink);
// Additionally appends every enabled message to a log file.
bool openLogFile(const std::filesystem::path& path);
void closeLogFile();

}  // namespace up::log

#define UP_LOG(level, subsystem, expr)                                        \
    do {                                                                      \
        if (::up::log::enabled(level, subsystem)) {                           \
            std::ostringstream up_log_os_;                                    \
            up_log_os_ << expr;                                               \
            ::up::log::write(level, subsystem, up_log_os_.str());             \
        }                                                                     \
    } while (0)

#define UP_LOG_DEBUG(sub, expr) UP_LOG(::up::log::Level::Debug, sub, expr)
#define UP_LOG_INFO(sub, expr) UP_LOG(::up::log::Level::Info, sub, expr)
#define UP_LOG_WARN(sub, expr) UP_LOG(::up::log::Level::Warning, sub, expr)
#define UP_LOG_ERROR(sub, expr) UP_LOG(::up::log::Level::Error, sub, expr)
