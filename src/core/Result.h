#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace up {

enum class ErrorCode {
    InvalidArgument,
    NotFound,
    IoError,
    ParseError,
    UnsupportedVersion,
    DecodeError,
    EncodeError,
    MediaOffline,
    OutOfRange,
    Conflict,
    Locked,
    Cancelled,
    Internal,
};

const char* toString(ErrorCode code);

// A human-readable error. `message` says what went wrong in user terms,
// `suggestion` says what to do about it, `details` carries technical context
// (library error strings, paths, values) for diagnostics.
struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string subsystem;
    std::string message;
    std::string suggestion;
    std::string details;

    // Stable identifier such as "UP-TIMELINE-OUT_OF_RANGE", suitable for logs and support.
    std::string id() const;
    // Multi-line text suitable for "copy diagnostics".
    std::string toString() const;
};

Error makeError(ErrorCode code, std::string subsystem, std::string message,
                std::string suggestion = {}, std::string details = {});

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    T& value() & { return std::get<T>(data_); }
    const T& value() const& { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    const Error& error() const { return std::get<Error>(data_); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

private:
    std::variant<T, Error> data_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    static Result success() { return {}; }

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return *error_; }

private:
    std::optional<Error> error_;
};

using Status = Result<void>;

}  // namespace up

// Propagates a failed Status/Result from the enclosing function.
#define UP_TRY(expr)                                   \
    do {                                               \
        auto up_try_result_ = (expr);                  \
        if (!up_try_result_.ok()) return up_try_result_.error(); \
    } while (0)
