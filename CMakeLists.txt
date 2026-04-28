cmake_minimum_required(VERSION 3.20)
project(predibloom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

option(BUILD_GUI "Build the raylib-based GUI (requires X11 dev headers on Linux)" ON)

# raylib v5.5
if(BUILD_GUI)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
    )
    FetchContent_MakeAvailable(raylib)
endif()

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

# indicators v2.3 (progress bars)
FetchContent_Declare(
    indicators
    GIT_REPOSITORY https://github.com/p-ranav/indicators.git
    GIT_TAG v2.3
)
FetchContent_MakeAvailable(indicators)

# Google Test v1.15.2
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Howard Hinnant's date / tz library v3.0.4 (IANA timezone support)
FetchContent_Declare(
    date
    GIT_REPOSITORY https://github.com/HowardHinnant/date.git
    GIT_TAG v3.0.4
)
set(USE_SYSTEM_TZ_DB ON CACHE BOOL "" FORCE)
set(BUILD_TZ_LIB ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(date)

# Find OpenSSL for cpp-httplib HTTPS support
find_package(OpenSSL REQUIRED)

# Find SQLite3 for NBM cache
find_package(SQLite3 REQUIRED)

# Find netCDF for NBM grid files
find_package(netCDF REQUIRED)

# Find ecCodes for GRIB2 parsing (brew install eccodes on macOS)
find_package(eccodes QUIET)
if(NOT eccodes_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ECCODES REQUIRED eccodes)
    set(ECCODES_LIBRARIES ${ECCODES_LINK_LIBRARIES})
    set(ECCODES_INCLUDE_DIRS ${ECCODES_INCLUDE_DIRS})
else()
    set(ECCODES_LIBRARIES eccodes)
    set(ECCODES_INCLUDE_DIRS "")
endif()

# Shared core library (API client + service layer)
add_library(predibloom_core STATIC
    src/api/kalshi_client.cpp
    src/api/kalshi_db.cpp
    src/api/local_kalshi_client.cpp
    src/api/gribstream_client.cpp
    src/api/local_nbm_client.cpp
    src/api/nbm_grid_reader.cpp
    src/api/nbm_downloader.cpp
    src/api/nbm_grib_parser.cpp
    src/api/nbm_service.cpp
    src/api/forecast_db.cpp
    src/api/nws_client.cpp
    src/api/http_cache.cpp
    src/core/service.cpp
    src/core/config.cpp
    src/core/datetime.cpp
    src/core/weather_comparison.cpp
    src/core/backtest_algo.cpp
    src/core/simple_algo.cpp
    src/core/latency_algo.cpp
)

target_include_directories(predibloom_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ECCODES_INCLUDE_DIRS}
)

target_link_libraries(predibloom_core PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite3::SQLite3
    netCDF::netcdf
    date::date-tz
    ${ECCODES_LIBRARIES}
)

target_compile_definitions(predibloom_core PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
)

# GUI executable
if(BUILD_GUI)
    add_executable(predibloom
        src/gui/main.cpp
        src/gui/app.cpp
        src/gui/control_socket.cpp
    )

    target_link_libraries(predibloom PRIVATE
        predibloom_core
        raylib
    )
endif()

# CLI executable
add_executable(predibloom-cli
    src/cli/main.cpp
    src/cli/commands/predict.cpp
    src/cli/commands/backtest.cpp
    src/cli/commands/portfolio.cpp
    src/cli/commands/misc.cpp
    src/cli/commands/nbm.cpp
    src/cli/commands/history.cpp
    src/cli/commands/kalshi_sync.cpp
)

target_link_libraries(predibloom-cli PRIVATE
    predibloom_core
    CLI11::CLI11
    indicators::indicators
)

# CLI test for API client (no GUI dependencies)
add_executable(test_api
    src/api/test_client.cpp
)

target_link_libraries(test_api PRIVATE
    predibloom_core
)

# Unit tests
enable_testing()

add_executable(predibloom_tests
    tests/test_result.cpp
    tests/test_types.cpp
    tests/test_gribstream_types.cpp
    tests/test_local_nbm_client.cpp
    tests/test_weather_comparison.cpp
    tests/test_config.cpp
    tests/test_formatters.cpp
    tests/test_rate_limiter.cpp
    tests/test_time_utils.cpp
)

target_link_libraries(predibloom_tests PRIVATE
    predibloom_core
    GTest::gtest_main
)

target_compile_definitions(predibloom_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data"
)

include(GoogleTest)
gtest_discover_tests(predibloom_tests)

# Integration tests (require API credentials)
add_executable(predibloom_integration_tests
    tests/test_gribstream_integration.cpp
)

target_link_libraries(predibloom_integration_tests PRIVATE
    predibloom_core
    GTest::gtest_main
)

gtest_discover_tests(predibloom_integration_tests)
