#pragma once

#include <optional>
#include <string>
#include <vector>

namespace predibloom::api {

// Reader for NBM grid files stored as NetCDF4.
// Grid files are organized as:
//   {base_path}/grids/blend.YYYYMMDD/HH/2t.fFFF.nc
//
// Each NetCDF4 file contains:
//   - latitude(y, x): Grid latitudes
//   - longitude(y, x): Grid longitudes
//   - temperature_2m(y, x): 2m temperature in Kelvin
class NbmGridReader {
public:
    explicit NbmGridReader(const std::string& base_path);
    ~NbmGridReader();

    NbmGridReader(const NbmGridReader&) = delete;
    NbmGridReader& operator=(const NbmGridReader&) = delete;

    // Get 2m temperature at nearest grid point (returns Kelvin).
    // Returns nullopt if file doesn't exist or read fails.
    std::optional<double> getTemp(const std::string& cycle_date,
                                   int cycle_hour,
                                   int forecast_hour,
                                   double lat, double lon);

    // Check if we have a specific grid file.
    bool hasFile(const std::string& cycle_date,
                 int cycle_hour,
                 int forecast_hour);

    // Get all available forecast hours for a cycle.
    std::vector<int> getAvailableForecastHours(const std::string& cycle_date,
                                                int cycle_hour);

    // Get the base path.
    const std::string& basePath() const { return base_path_; }

private:
    std::string gridFilePath(const std::string& cycle_date,
                             int cycle_hour,
                             int forecast_hour) const;

    std::string base_path_;
};

}  // namespace predibloom::api
