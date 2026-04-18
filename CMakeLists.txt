cmake_minimum_required(VERSION 3.20)
project(predibloom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# raylib v5.5
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

# cpp-httplib v0.15.3 (header-only, needs OpenSSL)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json v3.11.3
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# CLI11 v2.4.2
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# Find OpenSSL for cpp-httplib HTTPS support
find_package(OpenSSL REQUIRED)

# Shared core library (API client + service layer)
add_library(predibloom_core STATIC
    src/api/kalshi_client.cpp
    src/api/openmeteo_client.cpp
    src/api/nws_client.cpp
    src/api/http_cache.cpp
    src/core/service.cpp
    src/core/config.cpp
    src/core/weather_comparison.cpp
)

target_include_directories(predibloom_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(predibloom_core PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(predibloom_core PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
)

# GUI executable
add_executable(predibloom
    src/gui/main.cpp
    src/gui/app.cpp
)

target_link_libraries(predibloom PRIVATE
    predibloom_core
    raylib
)

# CLI executable
add_executable(predibloom-cli
    src/cli/main.cpp
)

target_link_libraries(predibloom-cli PRIVATE
    predibloom_core
    CLI11::CLI11
)

# CLI test for API client (no GUI dependencies)
add_executable(test_api
    src/api/test_client.cpp
)

target_link_libraries(test_api PRIVATE
    predibloom_core
)
