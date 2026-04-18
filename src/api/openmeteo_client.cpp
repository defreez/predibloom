#include "openmeteo_client.hpp"
#include "http_cache.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>

namespace predibloom::api {

namespace {
constexpr const char* ARCHIVE_HOST = "archive-api.open-meteo.com";
constexpr const char* FORECAST_HOST = "historical-forecast-api.open-meteo.com";
}

OpenMeteoClient::OpenMeteoClient()
    : archive_client_(std::make_unique<httplib::SSLClient>(ARCHIVE_HOST, 443))
    , forecast_client_(std::make_unique<httplib::SSLClient>(FORECAST_HOST, 443))
    , rate_limiter_(5) {  // 5 requests per second for free API
    archive_client_->set_connection_timeout(10);
    archive_client_->set_read_timeout(30);
    archive_client_->set_write_timeout(10);

    forecast_client_->set_connection_timeout(10);
    forecast_client_->set_read_timeout(30);
    forecast_client_->set_write_timeout(10);
}

OpenMeteoClient::~OpenMeteoClient() = default;

Result<WeatherResponse> OpenMeteoClient::getHistoricalWeather(
    double latitude,
    double longitude,
    const std::string& start_date,
    const std::string& end_date) {

    return fetchWeather(ARCHIVE_HOST, latitude, longitude, start_date, end_date);
}

Result<WeatherResponse> OpenMeteoClient::getHistoricalForecast(
    double latitude,
    double longitude,
    const std::string& start_date,
    const std::string& end_date) {

    return fetchWeather(FORECAST_HOST, latitude, longitude, start_date, end_date);
}

Result<WeatherResponse> OpenMeteoClient::fetchWeather(
    const std::string& host,
    double latitude,
    double longitude,
    const std::string& start_date,
    const std::string& end_date) {

    // Select the appropriate client
    httplib::SSLClient* client = nullptr;
    if (host == ARCHIVE_HOST) {
        client = archive_client_.get();
    } else {
        client = forecast_client_.get();
    }

    // Build query path
    // Archive API uses /v1/archive, historical-forecast API uses /v1/forecast
    std::ostringstream path_ss;
    const char* endpoint = (host == ARCHIVE_HOST) ? "/v1/archive?" : "/v1/forecast?";
    path_ss << endpoint
            << "latitude=" << latitude
            << "&longitude=" << longitude
            << "&start_date=" << start_date
            << "&end_date=" << end_date
            << "&daily=temperature_2m_max,temperature_2m_min"
            << "&temperature_unit=fahrenheit"
            << "&timezone=America/New_York";
    std::string path = path_ss.str();

    auto cache_key = HttpCache::key(host, path);

    std::string body;
    auto cached = caching_ ? HttpCache::get(cache_key) : std::nullopt;
    if (cached) {
        body = *cached;
    } else {
        rate_limiter_.wait_for_token();
        auto res = client->Get(path);

        if (!res) {
            return Error(ApiError::NetworkError,
                "Network error: " + httplib::to_string(res.error()));
        }

        if (res->status == 429) {
            return Error(ApiError::RateLimitError, "Rate limit exceeded", 429);
        }

        if (res->status != 200) {
            return Error(ApiError::HttpError,
                "HTTP error: " + std::to_string(res->status) + " - " + res->body,
                res->status);
        }

        body = res->body;
        if (caching_) HttpCache::put(cache_key, body);
    }

    try {
        auto json = nlohmann::json::parse(body);
        return json.get<WeatherResponse>();
    } catch (const nlohmann::json::exception& e) {
        return Error(ApiError::ParseError,
            std::string("JSON parse error: ") + e.what());
    }
}

} // namespace predibloom::api
