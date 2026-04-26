#pragma once

#include "result.hpp"

#include <optional>
#include <string>
#include <vector>

namespace predibloom::api {

// Variable info from GRIB2 inventory
struct GribVariable {
    std::string short_name;    // e.g., "2t"
    std::string type_of_level; // e.g., "heightAboveGround"
    int level;                 // e.g., 2
    std::string name;          // e.g., "2 metre temperature"
};

// 2D grid of temperature data
struct TempGrid {
    std::vector<float> lats;   // Latitude at each grid point (ny * nx)
    std::vector<float> lons;   // Longitude at each grid point (ny * nx)
    std::vector<float> temps;  // Temperature in Kelvin (ny * nx)
    size_t ny = 0;             // Number of rows
    size_t nx = 0;             // Number of columns

    // Get temperature at a specific point (nearest neighbor)
    std::optional<double> getTempAt(double lat, double lon) const;
};

// Parses GRIB2 files using ecCodes library
class NbmGribParser {
public:
    NbmGribParser();
    ~NbmGribParser();

    // Extract 2m temperature at a specific point
    // Returns temperature in Kelvin
    Result<double> getTempAtPoint(const std::string& grib_path,
                                   double lat, double lon);

    // Extract full 2m temperature grid
    Result<TempGrid> getFullGrid(const std::string& grib_path);

    // List all variables in a GRIB2 file
    Result<std::vector<GribVariable>> listVariables(const std::string& grib_path);

private:
    // Find the 2m temperature message in a GRIB file
    // Returns message handle or nullptr
    void* find2mTempMessage(void* file_handle);
};

}  // namespace predibloom::api
