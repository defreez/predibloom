#pragma once

#include <optional>
#include <string>

namespace predibloom::api {

class HttpCache {
public:
    // Returns cached response body if exists
    static std::optional<std::string> get(const std::string& cache_key);

    // Stores response body to cache
    static void put(const std::string& cache_key, const std::string& body);

    // Generate cache key from host and path (for GET)
    static std::string key(const std::string& host, const std::string& path);

    // Generate cache key from host, path, and body (for POST)
    static std::string key(const std::string& host, const std::string& path, const std::string& body);
};

} // namespace predibloom::api
