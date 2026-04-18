#include "http_cache.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

namespace predibloom::api {

namespace {

const std::string CACHE_DIR = ".cache/http";

std::string hash_to_hex(size_t hash) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

std::string cache_path(const std::string& cache_key) {
    size_t hash = std::hash<std::string>{}(cache_key);
    std::string hex = hash_to_hex(hash);
    return CACHE_DIR + "/" + hex + ".json";
}

void ensure_cache_dir() {
    std::filesystem::create_directories(CACHE_DIR);
}

} // namespace

std::string HttpCache::key(const std::string& host, const std::string& path) {
    return host + path;
}

std::optional<std::string> HttpCache::get(const std::string& cache_key) {
    std::string path = cache_path(cache_key);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    // First line is "URL: <key>\n", skip it
    std::string url_line;
    std::getline(file, url_line);

    // Rest is the cached body
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void HttpCache::put(const std::string& cache_key, const std::string& body) {
    ensure_cache_dir();
    std::string path = cache_path(cache_key);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return;  // Silently fail on write errors

    // Write URL as first line for debuggability
    file << "URL: " << cache_key << "\n";
    file << body;
}

} // namespace predibloom::api
