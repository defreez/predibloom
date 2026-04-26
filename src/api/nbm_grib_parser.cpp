#include "nbm_grib_parser.hpp"

#include <eccodes.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace predibloom::api {

std::optional<double> TempGrid::getTempAt(double lat, double lon) const {
    if (temps.empty() || lats.empty() || lons.empty()) {
        return std::nullopt;
    }

    // Find nearest grid point
    double min_dist = std::numeric_limits<double>::max();
    size_t best_idx = 0;

    for (size_t i = 0; i < lats.size(); ++i) {
        double dlat = lats[i] - lat;
        double dlon = lons[i] - lon;
        double dist = dlat * dlat + dlon * dlon;
        if (dist < min_dist) {
            min_dist = dist;
            best_idx = i;
        }
    }

    float temp = temps[best_idx];
    if (std::isnan(temp)) {
        return std::nullopt;
    }

    return static_cast<double>(temp);
}

NbmGribParser::NbmGribParser() = default;
NbmGribParser::~NbmGribParser() = default;

void* NbmGribParser::find2mTempMessage(void* file_handle) {
    codes_handle* h = nullptr;
    int err = 0;

    while ((h = codes_handle_new_from_file(nullptr, static_cast<FILE*>(file_handle),
                                            PRODUCT_GRIB, &err)) != nullptr) {
        if (err != CODES_SUCCESS) {
            if (h) codes_handle_delete(h);
            continue;
        }

        // Check shortName == "2t"
        char short_name[64] = {0};
        size_t len = sizeof(short_name);
        if (codes_get_string(h, "shortName", short_name, &len) != CODES_SUCCESS) {
            codes_handle_delete(h);
            continue;
        }

        if (strcmp(short_name, "2t") != 0) {
            codes_handle_delete(h);
            continue;
        }

        // Check typeOfLevel == "heightAboveGround"
        char type_of_level[64] = {0};
        len = sizeof(type_of_level);
        if (codes_get_string(h, "typeOfLevel", type_of_level, &len) != CODES_SUCCESS) {
            codes_handle_delete(h);
            continue;
        }

        if (strcmp(type_of_level, "heightAboveGround") != 0) {
            codes_handle_delete(h);
            continue;
        }

        // Check level == 2
        long level = 0;
        if (codes_get_long(h, "level", &level) != CODES_SUCCESS || level != 2) {
            codes_handle_delete(h);
            continue;
        }

        // Found it!
        return h;
    }

    return nullptr;
}

Result<double> NbmGribParser::getTempAtPoint(const std::string& grib_path,
                                               double lat, double lon) {
    FILE* f = fopen(grib_path.c_str(), "rb");
    if (!f) {
        return Error(ApiError::NetworkError, "Failed to open GRIB file: " + grib_path);
    }

    codes_handle* h = static_cast<codes_handle*>(find2mTempMessage(f));
    if (!h) {
        fclose(f);
        return Error(ApiError::ParseError, "No 2m temperature field found in GRIB file");
    }

    // Get grid dimensions
    long ni = 0, nj = 0;
    codes_get_long(h, "Ni", &ni);
    codes_get_long(h, "Nj", &nj);

    size_t num_points = static_cast<size_t>(ni * nj);

    // Get all lat/lon/values
    std::vector<double> lats(num_points);
    std::vector<double> lons(num_points);
    std::vector<double> values(num_points);

    size_t size = num_points;
    int err = codes_grib_get_data(h, lats.data(), lons.data(), values.data());
    if (err != CODES_SUCCESS) {
        codes_handle_delete(h);
        fclose(f);
        return Error(ApiError::ParseError,
                     std::string("Failed to get GRIB data: ") + codes_get_error_message(err));
    }

    // Find nearest point
    double min_dist = std::numeric_limits<double>::max();
    size_t best_idx = 0;

    for (size_t i = 0; i < num_points; ++i) {
        double dlat = lats[i] - lat;
        double dlon = lons[i] - lon;
        double dist = dlat * dlat + dlon * dlon;
        if (dist < min_dist) {
            min_dist = dist;
            best_idx = i;
        }
    }

    double temp_k = values[best_idx];

    codes_handle_delete(h);
    fclose(f);

    if (std::isnan(temp_k)) {
        return Error(ApiError::DataError, "Temperature value is NaN at requested location");
    }

    return temp_k;
}

Result<TempGrid> NbmGribParser::getFullGrid(const std::string& grib_path) {
    FILE* f = fopen(grib_path.c_str(), "rb");
    if (!f) {
        return Error(ApiError::NetworkError, "Failed to open GRIB file: " + grib_path);
    }

    codes_handle* h = static_cast<codes_handle*>(find2mTempMessage(f));
    if (!h) {
        fclose(f);
        return Error(ApiError::ParseError, "No 2m temperature field found in GRIB file");
    }

    // Get grid dimensions
    long ni = 0, nj = 0;
    codes_get_long(h, "Ni", &ni);
    codes_get_long(h, "Nj", &nj);

    size_t num_points = static_cast<size_t>(ni * nj);

    TempGrid grid;
    grid.nx = static_cast<size_t>(ni);
    grid.ny = static_cast<size_t>(nj);
    grid.lats.resize(num_points);
    grid.lons.resize(num_points);
    grid.temps.resize(num_points);

    // Get all data
    std::vector<double> lats(num_points);
    std::vector<double> lons(num_points);
    std::vector<double> values(num_points);

    int err = codes_grib_get_data(h, lats.data(), lons.data(), values.data());
    if (err != CODES_SUCCESS) {
        codes_handle_delete(h);
        fclose(f);
        return Error(ApiError::ParseError,
                     std::string("Failed to get GRIB data: ") + codes_get_error_message(err));
    }

    // Convert to float arrays
    for (size_t i = 0; i < num_points; ++i) {
        grid.lats[i] = static_cast<float>(lats[i]);
        grid.lons[i] = static_cast<float>(lons[i]);
        grid.temps[i] = static_cast<float>(values[i]);
    }

    codes_handle_delete(h);
    fclose(f);

    return grid;
}

Result<std::vector<GribVariable>> NbmGribParser::listVariables(const std::string& grib_path) {
    FILE* f = fopen(grib_path.c_str(), "rb");
    if (!f) {
        return Error(ApiError::NetworkError, "Failed to open GRIB file: " + grib_path);
    }

    std::vector<GribVariable> variables;
    codes_handle* h = nullptr;
    int err = 0;

    while ((h = codes_handle_new_from_file(nullptr, f, PRODUCT_GRIB, &err)) != nullptr) {
        if (err != CODES_SUCCESS) {
            if (h) codes_handle_delete(h);
            continue;
        }

        GribVariable var;

        // shortName
        char buf[256] = {0};
        size_t len = sizeof(buf);
        if (codes_get_string(h, "shortName", buf, &len) == CODES_SUCCESS) {
            var.short_name = buf;
        }

        // typeOfLevel
        len = sizeof(buf);
        if (codes_get_string(h, "typeOfLevel", buf, &len) == CODES_SUCCESS) {
            var.type_of_level = buf;
        }

        // level
        long level = 0;
        if (codes_get_long(h, "level", &level) == CODES_SUCCESS) {
            var.level = static_cast<int>(level);
        }

        // name
        len = sizeof(buf);
        if (codes_get_string(h, "name", buf, &len) == CODES_SUCCESS) {
            var.name = buf;
        }

        variables.push_back(var);
        codes_handle_delete(h);
    }

    fclose(f);
    return variables;
}

}  // namespace predibloom::api
