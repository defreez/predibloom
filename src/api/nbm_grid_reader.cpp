#include "nbm_grid_reader.hpp"

#include <netcdf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <vector>

namespace predibloom::api {

namespace {

// Check netCDF return code and throw on error.
void checkNc(int status, const char* msg) {
    if (status != NC_NOERR) {
        throw std::runtime_error(std::string(msg) + ": " + nc_strerror(status));
    }
}

}  // namespace

NbmGridReader::NbmGridReader(const std::string& base_path)
    : base_path_(base_path) {
}

NbmGridReader::~NbmGridReader() = default;

std::string NbmGridReader::gridFilePath(const std::string& cycle_date,
                                         int cycle_hour,
                                         int forecast_hour) const {
    // Format: {base}/grids/blend.YYYYMMDD/HH/2t.fFFF.nc
    std::string date_str = cycle_date;
    // Remove dashes: 2026-04-24 -> 20260424
    date_str.erase(std::remove(date_str.begin(), date_str.end(), '-'),
                   date_str.end());

    std::ostringstream ss;
    ss << base_path_ << "/grids/blend." << date_str
       << "/" << std::setw(2) << std::setfill('0') << cycle_hour
       << "/2t.f" << std::setw(3) << std::setfill('0') << forecast_hour
       << ".nc";
    return ss.str();
}

bool NbmGridReader::hasFile(const std::string& cycle_date,
                             int cycle_hour,
                             int forecast_hour) {
    return std::filesystem::exists(gridFilePath(cycle_date, cycle_hour, forecast_hour));
}

std::vector<int> NbmGridReader::getAvailableForecastHours(
    const std::string& cycle_date, int cycle_hour) {

    std::vector<int> hours;

    std::string date_str = cycle_date;
    date_str.erase(std::remove(date_str.begin(), date_str.end(), '-'),
                   date_str.end());

    std::ostringstream dir_ss;
    dir_ss << base_path_ << "/grids/blend." << date_str
           << "/" << std::setw(2) << std::setfill('0') << cycle_hour;

    std::filesystem::path dir_path(dir_ss.str());
    if (!std::filesystem::exists(dir_path)) {
        return hours;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            // Parse "2t.fFFF.nc"
            if (filename.size() >= 10 &&
                filename.substr(0, 4) == "2t.f" &&
                filename.substr(filename.size() - 3) == ".nc") {
                try {
                    int fhr = std::stoi(filename.substr(4, 3));
                    hours.push_back(fhr);
                } catch (...) {
                    // Skip malformed filenames
                }
            }
        }
    }

    std::sort(hours.begin(), hours.end());
    return hours;
}

std::optional<double> NbmGridReader::getTemp(const std::string& cycle_date,
                                              int cycle_hour,
                                              int forecast_hour,
                                              double lat, double lon) {
    std::string path = gridFilePath(cycle_date, cycle_hour, forecast_hour);

    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    int ncid = -1;
    int status = nc_open(path.c_str(), NC_NOWRITE, &ncid);
    if (status != NC_NOERR) {
        return std::nullopt;
    }

    try {
        // Get dimensions
        int y_dimid, x_dimid;
        checkNc(nc_inq_dimid(ncid, "y", &y_dimid), "get y dim");
        checkNc(nc_inq_dimid(ncid, "x", &x_dimid), "get x dim");

        size_t ny, nx;
        checkNc(nc_inq_dimlen(ncid, y_dimid, &ny), "get y len");
        checkNc(nc_inq_dimlen(ncid, x_dimid, &nx), "get x len");

        // Get variable IDs
        int lat_varid, lon_varid, temp_varid;
        checkNc(nc_inq_varid(ncid, "latitude", &lat_varid), "get lat var");
        checkNc(nc_inq_varid(ncid, "longitude", &lon_varid), "get lon var");
        checkNc(nc_inq_varid(ncid, "temperature_2m", &temp_varid), "get temp var");

        // Read full latitude and longitude arrays
        std::vector<float> lats(ny * nx);
        std::vector<float> lons(ny * nx);
        checkNc(nc_get_var_float(ncid, lat_varid, lats.data()), "read lat");
        checkNc(nc_get_var_float(ncid, lon_varid, lons.data()), "read lon");

        // Find nearest grid point
        double min_dist = std::numeric_limits<double>::max();
        size_t best_idx = 0;

        for (size_t i = 0; i < ny * nx; ++i) {
            double dlat = lats[i] - lat;
            double dlon = lons[i] - lon;
            double dist = dlat * dlat + dlon * dlon;
            if (dist < min_dist) {
                min_dist = dist;
                best_idx = i;
            }
        }

        // Read temperature at that point
        size_t y_idx = best_idx / nx;
        size_t x_idx = best_idx % nx;

        size_t start[2] = {y_idx, x_idx};
        size_t count[2] = {1, 1};
        float temp_k;
        checkNc(nc_get_vara_float(ncid, temp_varid, start, count, &temp_k),
                "read temp");

        nc_close(ncid);

        // Check for fill value / NaN
        if (std::isnan(temp_k)) {
            return std::nullopt;
        }

        return static_cast<double>(temp_k);

    } catch (const std::exception&) {
        nc_close(ncid);
        return std::nullopt;
    }
}

}  // namespace predibloom::api
