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

# Find OpenSSL for cpp-httplib HTTPS support
find_package(OpenSSL REQUIRED)

add_executable(predibloom
    src/main.cpp
    src/app.cpp
)

target_link_libraries(predibloom PRIVATE
    raylib
    httplib::httplib
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(predibloom PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
