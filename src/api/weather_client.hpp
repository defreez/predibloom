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

    // Forecast for a single local NY date.
    virtual Result<WeatherResponse> getForecast(double latitude,
                                                  double longitude,
                                                  const std::string& date,
                                                  const std::string& asOf_iso = "") = 0;

    // Actuals for a single local NY date.
    virtual Result<WeatherResponse> getActuals(double latitude,
                                                 double longitude,
                                                 const std::string& date) = 0;

    virtual void setCaching(bool enabled) = 0;
};

// Wrapper for GribStreamClient
class GribStreamWeatherClient : public WeatherClient {
public:
    explicit GribStreamWeatherClient(const std::string& api_token)
        : client_(api_token) {}

    Result<WeatherResponse> getForecast(double latitude,
                                          double longitude,
                                          const std::string& date,
                                          const std::string& asOf_iso = "") override {
        return client_.getForecast(latitude, longitude, date, asOf_iso);
    }

    Result<WeatherResponse> getActuals(double latitude,
                                         double longitude,
                                         const std::string& date) override {
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
                                          const std::string& asOf_iso = "") override {
        return client_.getForecast(latitude, longitude, date, asOf_iso);
    }

    Result<WeatherResponse> getActuals(double latitude,
                                         double longitude,
                                         const std::string& date) override {
        return client_.getActuals(latitude, longitude, date);
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
