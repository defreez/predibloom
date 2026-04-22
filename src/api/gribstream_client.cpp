#include "gribstream_client.hpp"
#include "http_cache.hpp"
#include "../core/time_utils.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace predibloom::api {

namespace {
constexpr const char* API_HOST = "gribstream.com";
constexpr const char* API_PATH = "/api/v2/nbm/timeseries";
}

bool parseGribstreamCsvTemps(const std::string& csv_body, std::vector<double>& out_kelvin) {
    out_kelvin.clear();
    std::istringstream iss(csv_body);
    std::string line;

    // Skip header
    if (!std::getline(iss, line)) return false;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        // Temp is the last column. Trim trailing whitespace/\r first.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t last_comma = line.rfind(',');
        if (last_comma == std::string::npos) continue;
        std::string tok = line.substr(last_comma + 1);
        // Trim leading whitespace (GribStream sometimes indents rows)
        size_t first = tok.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        tok = tok.substr(first);
        try {
            double v = std::stod(tok);
            out_kelvin.push_back(v);
        } catch (...) {
            // skip unparseable
        }
    }
    return true;
}

GribStreamClient::GribStreamClient(const std::string& api_token)
    : client_(std::make_unique<httplib::SSLClient>(API_HOST, 443))
    , rate_limiter_(10)
    , api_token_(api_token) {
    client_->set_connection_timeout(10);
    client_->set_read_timeout(60);
    client_->set_write_timeout(10);
}

GribStreamClient::~GribStreamClient() = default;

Result<WeatherResponse> GribStreamClient::getForecast(double latitude,
                                                       double longitude,
                                                       const std::string& date,
                                                       const std::string& asOf_iso) {
    return fetchAggregated(latitude, longitude, date, asOf_iso, /*actuals=*/false);
}

Result<WeatherResponse> GribStreamClient::getActuals(double latitude,
                                                      double longitude,
                                                      const std::string& date) {
    return fetchAggregated(latitude, longitude, date, "", /*actuals=*/true);
}

Result<WeatherResponse> GribStreamClient::fetchAggregated(double latitude,
                                                           double longitude,
                                                           const std::string& date,
                                                           const std::string& asOf_iso,
                                                           bool actuals) {
    if (api_token_.empty()) {
        return Error(ApiError::HttpError,
            "GribStream API token not configured (set gribstream_api_token in auth.json)");
    }

    std::string from_iso = core::nyMidnightToUtcIso(date);
    std::string until_iso = core::nyMidnightToUtcIso(core::addDaysToDate(date, 1));
    if (from_iso.empty() || until_iso.empty()) {
        return Error(ApiError::ParseError, "Invalid date: " + date);
    }

    nlohmann::json body;
    body["fromTime"] = from_iso;
    body["untilTime"] = until_iso;
    body["coordinates"] = nlohmann::json::array({
        { {"lat", latitude}, {"lon", longitude} }
    });
    body["variables"] = nlohmann::json::array({
        { {"name", "TMP"}, {"level", "2 m above ground"}, {"alias", "temp"} }
    });
    if (actuals) {
        body["maxLeadTime"] = "1h";
    } else if (!asOf_iso.empty()) {
        body["asOf"] = asOf_iso;
    }

    std::string body_str = body.dump();
    std::string cache_key = HttpCache::key(API_HOST, API_PATH, body_str);

    std::string csv;
    auto cached = caching_ ? HttpCache::get(cache_key) : std::nullopt;
    if (cached) {
        csv = *cached;
    } else {
        rate_limiter_.wait_for_token();

        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_token_},
            {"Accept", "text/csv"}
        };
        auto res = client_->Post(API_PATH, headers, body_str, "application/json");

        if (!res) {
            return Error(ApiError::NetworkError,
                "Network error: " + httplib::to_string(res.error()));
        }
        if (res->status == 429) {
            return Error(ApiError::RateLimitError, "Rate limit exceeded", 429);
        }
        if (res->status == 401 || res->status == 403) {
            return Error(ApiError::HttpError,
                "GribStream auth failed (" + std::to_string(res->status) + "). Check gribstream_api_token.",
                res->status);
        }
        if (res->status != 200) {
            return Error(ApiError::HttpError,
                "HTTP error: " + std::to_string(res->status) + " - " + res->body,
                res->status);
        }
        csv = res->body;
        if (caching_) HttpCache::put(cache_key, csv);
    }

    std::vector<double> temps_k;
    parseGribstreamCsvTemps(csv, temps_k);

    WeatherResponse resp;
    resp.latitude = latitude;
    resp.longitude = longitude;
    resp.timezone = "America/New_York";
    resp.daily.time = {date};

    if (temps_k.empty()) {
        double nan = std::numeric_limits<double>::quiet_NaN();
        resp.daily.temperature_2m_max = {nan};
        resp.daily.temperature_2m_min = {nan};
    } else {
        double max_k = *std::max_element(temps_k.begin(), temps_k.end());
        double min_k = *std::min_element(temps_k.begin(), temps_k.end());
        resp.daily.temperature_2m_max = {kelvinToFahrenheit(max_k)};
        resp.daily.temperature_2m_min = {kelvinToFahrenheit(min_k)};
    }

    return resp;
}

} // namespace predibloom::api
