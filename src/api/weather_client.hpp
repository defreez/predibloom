#pragma once

#include "gribstream_types.hpp"
#include "gribstream_client.hpp"
#include "local_nbm_client.hpp"
#include "result.hpp"
#include "../core/config.hpp"
#include <memory>
#include <string>
#include <variant>

namespace predibloom::api {

// Unified interface for weather clients.
// Wraps either GribStreamClient or LocalNbmClient based on configuration.
class WeatherClient {
public:
    // Create a weather client based on the weather source
    static std::unique_ptr<WeatherClient> create(core::WeatherSource source,
                                                   const std::string& gribstream_token = "");

    virtual ~WeatherClient() = default;

    // Forecast for a single local date.
    // utc_offset_hours: timezone offset from UTC (e.g., -5 for EST, -8 for PST)
    virtual Result<WeatherResponse> getForecast(double latitude,
                                                  double longitude,
                                                  const std::string& date,
                                                  int utc_offset_hours,
                                                  const std::string& asOf_iso = "") = 0;

    // Actuals for a single local date.
    virtual Result<WeatherResponse> getActuals(double latitude,
                                                 double longitude,
                                                 const std::string& date,
                                                 int utc_offset_hours) = 0;

    virtual void setCaching(bool enabled) = 0;
};

// Wrapper for GribStreamClient
// Note: GribStream handles timezone internally, so utc_offset_hours is ignored.
class GribStreamWeatherClient : public WeatherClient {
public:
    explicit GribStreamWeatherClient(const std::string& api_token)
        : client_(api_token) {}

    Result<WeatherResponse> getForecast(double latitude,
                                          double longitude,
                                          const std::string& date,
                                          int /*utc_offset_hours*/,
                                          const std::string& asOf_iso = "") override {
        return client_.getForecast(latitude, longitude, date, asOf_iso);
    }

    Result<WeatherResponse> getActuals(double latitude,
                                         double longitude,
                                         const std::string& date,
                                         int /*utc_offset_hours*/) override {
        return client_.getActuals(latitude, longitude, date);
    }

    void setCaching(bool enabled) override {
        client_.setCaching(enabled);
    }

private:
    GribStreamClient client_;
};

// Wrapper for LocalNbmClient
class LocalNbmWeatherClient : public WeatherClient {
public:
    LocalNbmWeatherClient() = default;

    Result<WeatherResponse> getForecast(double latitude,
                                          double longitude,
                                          const std::string& date,
                                          int utc_offset_hours,
                                          const std::string& asOf_iso = "") override {
        return client_.getForecast(latitude, longitude, date, utc_offset_hours, asOf_iso);
    }

    Result<WeatherResponse> getActuals(double latitude,
                                         double longitude,
                                         const std::string& date,
                                         int utc_offset_hours) override {
        return client_.getActuals(latitude, longitude, date, utc_offset_hours);
    }

    void setCaching(bool enabled) override {
        client_.setCaching(enabled);
    }

private:
    LocalNbmClient client_;
};

// Factory implementation
inline std::unique_ptr<WeatherClient> WeatherClient::create(core::WeatherSource source,
                                                              const std::string& gribstream_token) {
    switch (source) {
        case core::WeatherSource::LocalNbm:
            return std::make_unique<LocalNbmWeatherClient>();
        case core::WeatherSource::GribStream:
        default:
            return std::make_unique<GribStreamWeatherClient>(gribstream_token);
    }
}

} // namespace predibloom::api
