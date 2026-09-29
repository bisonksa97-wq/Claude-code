#include "core/Result.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace up {

const char* toString(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument: return "INVALID_ARGUMENT";
        case ErrorCode::NotFound: return "NOT_FOUND";
        case ErrorCode::IoError: return "IO_ERROR";
        case ErrorCode::ParseError: return "PARSE_ERROR";
        case ErrorCode::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case ErrorCode::DecodeError: return "DECODE_ERROR";
        case ErrorCode::EncodeError: return "ENCODE_ERROR";
        case ErrorCode::MediaOffline: return "MEDIA_OFFLINE";
        case ErrorCode::OutOfRange: return "OUT_OF_RANGE";
        case ErrorCode::Conflict: return "CONFLICT";
        case ErrorCode::Locked: return "LOCKED";
        case ErrorCode::Cancelled: return "CANCELLED";
        case ErrorCode::Internal: return "INTERNAL";
    }
    return "UNKNOWN";
}

std::string Error::id() const {
    std::string sub = subsystem.empty() ? std::string("APP") : subsystem;
    std::transform(sub.begin(), sub.end(), sub.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return "UP-" + sub + "-" + up::toString(code);
}

std::string Error::toString() const {
    std::ostringstream os;
    os << message;
    if (!suggestion.empty()) os << "\nSuggested action: " << suggestion;
    os << "\nError ID: " << id();
    if (!details.empty()) os << "\nTechnical details: " << details;
    return os.str();
}

Error makeError(ErrorCode code, std::string subsystem, std::string message,
                std::string suggestion, std::string details) {
    return Error{code, std::move(subsystem), std::move(message), std::move(suggestion),
                 std::move(details)};
}

}  // namespace up
