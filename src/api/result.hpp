#pragma once

#include <string>
#include <variant>

namespace predibloom::api {

enum class ApiError {
    NetworkError,
    HttpError,
    ParseError,
    RateLimitError
};

struct Error {
    ApiError type;
    std::string message;
    int http_status = 0;

    Error(ApiError t, std::string msg, int status = 0)
        : type(t), message(std::move(msg)), http_status(status) {}
};

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    bool is_error() const { return std::holds_alternative<Error>(data_); }

    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }

    const Error& error() const { return std::get<Error>(data_); }

    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }

private:
    std::variant<T, Error> data_;
};

} // namespace predibloom::api
