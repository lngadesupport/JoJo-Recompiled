#pragma once
#include <string>
#include <utility>

namespace jojo {
enum class ErrorCode {
    none = 0,
    invalid_argument,
    unsupported_format,
    file_not_found,
    io_error,
    invalid_settings,
    invalid_installation,
    backend_unavailable,
    unknown_revision
};

template <typename T>
struct Result {
    T value{};
    ErrorCode error{ErrorCode::none};
    std::string detail{};
    [[nodiscard]] explicit operator bool() const noexcept { return error == ErrorCode::none; }
    static Result success(T v) { return Result{std::move(v), ErrorCode::none, {}}; }
    static Result failure(ErrorCode e, std::string d) { return Result{T{}, e, std::move(d)}; }
};

template <>
struct Result<void> {
    ErrorCode error{ErrorCode::none};
    std::string detail{};
    [[nodiscard]] explicit operator bool() const noexcept { return error == ErrorCode::none; }
    static Result success() { return {}; }
    static Result failure(ErrorCode e, std::string d) { return Result{e, std::move(d)}; }
};
}
