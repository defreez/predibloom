#include "nws_client.hpp"

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

    auto res = client_->Get(path);

    if (!res) {
        return Error(ApiError::NetworkError,
            "Network error: " + httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        return Error(ApiError::HttpError,
            "HTTP error: " + std::to_string(res->status), res->status);
    }

    try {
        auto json = nlohmann::json::parse(res->body);
        std::vector<CliObservation> observations;

        if (!json.contains("results") || !json["results"].is_array()) {
            return observations;  // Empty result
        }

        for (const auto& item : json["results"]) {
            CliObservation obs;
            obs.station = item.value("station", "");
            obs.date = item.value("valid", "");

            // High/low are required
            if (!item.contains("high") || item["high"].is_null() ||
                !item.contains("low") || item["low"].is_null()) {
                continue;  // Skip incomplete records
            }

            obs.high = item["high"].get<int>();
            obs.low = item["low"].get<int>();

            // Precip/snow are optional
            if (item.contains("precip") && item["precip"].is_number()) {
                obs.precip = item["precip"].get<double>();
            }
            if (item.contains("snow") && item["snow"].is_number()) {
                obs.snow = item["snow"].get<double>();
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
