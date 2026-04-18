#include "nws_client.hpp"
#include "http_cache.hpp"

#include <optional>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace predibloom::api {

namespace {
// Iowa Environmental Mesonet hosts archived NWS CLI data
constexpr const char* IEM_HOST = "mesonet.agron.iastate.edu";
}

NwsClient::NwsClient()
    : client_(std::make_unique<httplib::SSLClient>(IEM_HOST, 443)) {
    client_->set_connection_timeout(10);
    client_->set_read_timeout(30);
    client_->set_write_timeout(10);
}

NwsClient::~NwsClient() = default;

Result<std::vector<CliObservation>> NwsClient::getCliData(
    const std::string& station,
    int year) {

    std::string path = "/json/cli.py?station=" + station +
                       "&year=" + std::to_string(year);

    auto cache_key = HttpCache::key(IEM_HOST, path);

    std::string body;
    auto cached = caching_ ? HttpCache::get(cache_key) : std::nullopt;
    if (cached) {
        body = *cached;
    } else {
        auto res = client_->Get(path);

        if (!res) {
            return Error(ApiError::NetworkError,
                "Network error: " + httplib::to_string(res.error()));
        }

        if (res->status != 200) {
            return Error(ApiError::HttpError,
                "HTTP error: " + std::to_string(res->status), res->status);
        }

        body = res->body;
        if (caching_) HttpCache::put(cache_key, body);
    }

    try {
        auto json = nlohmann::json::parse(body);
        std::vector<CliObservation> observations;

        if (!json.contains("results") || !json["results"].is_array()) {
            return observations;  // Empty result
        }

        for (const auto& item : json["results"]) {
            CliObservation obs;
            obs.station = item.value("station", "");
            obs.date = item.value("valid", "");

            // High/low are required (NWS API sometimes returns these as strings)
            if (!item.contains("high") || item["high"].is_null() ||
                !item.contains("low") || item["low"].is_null()) {
                continue;  // Skip incomplete records
            }

            auto parse_int = [](const nlohmann::json& v) -> int {
                if (v.is_number()) return v.get<int>();
                if (v.is_string()) return std::stoi(v.get<std::string>());
                throw nlohmann::json::type_error::create(302,
                    "type must be number or string, but is " + std::string(v.type_name()), &v);
            };

            try {
                obs.high = parse_int(item["high"]);
                obs.low = parse_int(item["low"]);
            } catch (...) {
                continue;  // Skip unparseable records
            }

            // Precip/snow are optional (may also be strings)
            auto parse_double = [](const nlohmann::json& v) -> std::optional<double> {
                if (v.is_number()) return v.get<double>();
                if (v.is_string()) {
                    try { return std::stod(v.get<std::string>()); }
                    catch (...) { return std::nullopt; }
                }
                return std::nullopt;
            };

            if (item.contains("precip") && !item["precip"].is_null()) {
                if (auto v = parse_double(item["precip"])) obs.precip = *v;
            }
            if (item.contains("snow") && !item["snow"].is_null()) {
                if (auto v = parse_double(item["snow"])) obs.snow = *v;
            }

            observations.push_back(std::move(obs));
        }

        return observations;
    } catch (const nlohmann::json::exception& e) {
        return Error(ApiError::ParseError,
            std::string("JSON parse error: ") + e.what());
    }
}

Result<std::optional<CliObservation>> NwsClient::getCliForDate(
    const std::string& station,
    const std::string& date) {

    // Extract year from date (YYYY-MM-DD)
    if (date.size() < 4) {
        return Error(ApiError::ParseError, "Invalid date format");
    }

    int year = std::stoi(date.substr(0, 4));
    auto result = getCliData(station, year);

    if (!result.ok()) {
        return result.error();
    }

    for (const auto& obs : result.value()) {
        if (obs.date == date) {
            return std::optional<CliObservation>(obs);
        }
    }

    return std::optional<CliObservation>(std::nullopt);
}

Result<std::optional<int>> NwsClient::getHighForDate(
    const std::string& station,
    const std::string& date) {

    auto result = getCliForDate(station, date);

    if (!result.ok()) {
        return result.error();
    }

    if (result.value().has_value()) {
        return std::optional<int>(result.value()->high);
    }

    return std::optional<int>(std::nullopt);
}

}  // namespace predibloom::api
