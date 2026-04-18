#pragma once

#include "result.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace httplib {
class SSLClient;
}

namespace predibloom::api {

struct CliObservation {
    std::string station;
    std::string date;  // YYYY-MM-DD
    int high;          // Daily high temperature in Fahrenheit
    int low;           // Daily low temperature in Fahrenheit
    std::optional<double> precip;
    std::optional<double> snow;
};

// Client for NWS Daily Climate Report data via Iowa Environmental Mesonet
// This is the authoritative source for Kalshi weather market settlements
class NwsClient {
public:
    NwsClient();
    ~NwsClient();

    NwsClient(const NwsClient&) = delete;
    NwsClient& operator=(const NwsClient&) = delete;

    // Get CLI observations for a station and year
    // Returns all available observations for that year
    Result<std::vector<CliObservation>> getCliData(
        const std::string& station,
        int year);

    // Get CLI observation for a specific date
    // Returns empty optional if not found
    Result<std::optional<CliObservation>> getCliForDate(
        const std::string& station,
        const std::string& date);  // YYYY-MM-DD

    // Get high temperature for a specific date (convenience method)
    // This is what Kalshi uses for settlement
    Result<std::optional<int>> getHighForDate(
        const std::string& station,
        const std::string& date);

    void setCaching(bool enabled) { caching_ = enabled; }

private:
    std::unique_ptr<httplib::SSLClient> client_;
    bool caching_ = false;
};

// Station identifiers for common markets
namespace stations {
    constexpr const char* NYC_CENTRAL_PARK = "KNYC";
    constexpr const char* LA_AIRPORT = "KLAX";
    // Add more as needed
}

}  // namespace predibloom::api
