#include <gtest/gtest.h>
#include "api/forecast_db.hpp"
#include "api/local_nbm_client.hpp"
#include "api/weather_client.hpp"
#include "core/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace predibloom::api;
using namespace predibloom::core;

namespace {

// Scoped tmp directory that removes itself on destruction.
class TempDir {
public:
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() /
                            "predibloom_nbm_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char* dir = mkdtemp(buf.data());
        if (dir) path_ = dir;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

}  // namespace

// --- WeatherClient factory tests ---

TEST(WeatherClientFactory, CreateGribStream) {
    auto client = WeatherClient::create(WeatherSource::GribStream, "test_token");
    EXPECT_NE(client, nullptr);
}

TEST(WeatherClientFactory, CreateLocalNbm) {
    auto client = WeatherClient::create(WeatherSource::LocalNbm, "");
    EXPECT_NE(client, nullptr);
}

// --- ForecastDb tests ---

TEST(ForecastDb, CreateDatabase) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    ForecastDb db(db_path);
    EXPECT_TRUE(db.is_open());
    EXPECT_TRUE(std::filesystem::exists(db_path));
}

TEST(ForecastDb, PutAndGet) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    ForecastDb db(db_path);

    DailyForecast f;
    f.source = "nbm";
    f.target_date = "2026-04-25";
    f.cycle_hour = 19;
    f.latitude = 40.758;
    f.longitude = -73.985;
    f.temp_max_f = 72.5;
    f.temp_min_f = 58.3;
    f.cycle_date = "2026-04-24";
    f.hours_fetched = 24;

    EXPECT_TRUE(db.putNbm(f));

    auto result = db.getNbm("2026-04-25", 19, 40.758, -73.985);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->target_date, "2026-04-25");
    EXPECT_EQ(result->cycle_hour, 19);
    EXPECT_DOUBLE_EQ(result->temp_max_f, 72.5);
    EXPECT_DOUBLE_EQ(result->temp_min_f, 58.3);
    EXPECT_EQ(result->hours_fetched, 24);
}

TEST(ForecastDb, LatLonTolerance) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    ForecastDb db(db_path);

    DailyForecast f;
    f.source = "nbm";
    f.target_date = "2026-04-25";
    f.cycle_hour = 19;
    f.latitude = 40.758;
    f.longitude = -73.985;
    f.temp_max_f = 72.5;
    f.temp_min_f = 58.3;
    f.cycle_date = "2026-04-24";
    f.hours_fetched = 24;

    EXPECT_TRUE(db.putNbm(f));

    // Within tolerance (0.0005 degrees ~= 55m)
    auto result1 = db.getNbm("2026-04-25", 19, 40.7582, -73.9848);
    EXPECT_TRUE(result1.has_value());

    // Outside tolerance
    auto result2 = db.getNbm("2026-04-25", 19, 40.760, -73.985);
    EXPECT_FALSE(result2.has_value());
}

TEST(ForecastDb, CacheMissReturnsNullopt) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    ForecastDb db(db_path);

    auto result = db.getNbm("2026-04-25", 19, 40.758, -73.985);
    EXPECT_FALSE(result.has_value());
}

TEST(ForecastDb, Upsert) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    ForecastDb db(db_path);

    DailyForecast f;
    f.source = "nbm";
    f.target_date = "2026-04-25";
    f.cycle_hour = 19;
    f.latitude = 40.758;
    f.longitude = -73.985;
    f.temp_max_f = 72.5;
    f.temp_min_f = 58.3;
    f.cycle_date = "2026-04-24";
    f.hours_fetched = 24;

    EXPECT_TRUE(db.putNbm(f));

    // Update the same record.
    f.temp_max_f = 75.0;
    EXPECT_TRUE(db.putNbm(f));

    auto result = db.getNbm("2026-04-25", 19, 40.758, -73.985);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->temp_max_f, 75.0);
}

// --- LocalNbmClient interface tests ---

TEST(LocalNbmClient, ConstructorDestructor) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    LocalNbmClient client(db_path);
    EXPECT_TRUE(client.is_open());
}

TEST(LocalNbmClient, CacheMissReturnsError) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();
    LocalNbmClient client(db_path);

    // Disable grid reader to ensure we get a cache miss error.
    client.setGridPath("");

    // Cache miss returns error when neither SQLite cache nor grid files are available.
    auto result = client.getForecast(40.758, -73.985, "2026-04-25", "America/New_York");
    EXPECT_TRUE(result.is_error());
    // The error message mentions a coverage problem for the target date.
    EXPECT_NE(result.error().message.find("No NBM cycle"), std::string::npos);
}

TEST(LocalNbmClient, GetForecastFromDb) {
    TempDir tmp;
    std::string db_path = (tmp.path() / "test.db").string();

    // Pre-populate the database.
    {
        ForecastDb db(db_path);
        DailyForecast f;
        f.source = "nbm";
        f.target_date = "2026-04-25";
        f.cycle_hour = 19;
        f.latitude = 40.758;
        f.longitude = -73.985;
        f.temp_max_f = 72.5;
        f.temp_min_f = 58.3;
        f.time_of_max = "2026-04-25T15:00:00Z";
        f.time_of_min = "2026-04-25T06:00:00Z";
        f.cycle_date = "2026-04-24";
        f.hours_fetched = 24;
        db.putNbm(f);
    }

    LocalNbmClient client(db_path);
    auto result = client.getForecast(40.758, -73.985, "2026-04-25", "America/New_York");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result->daily.time.size(), 1u);
    EXPECT_EQ(result->daily.time[0], "2026-04-25");
    EXPECT_DOUBLE_EQ(result->daily.temperature_2m_max[0], 72.5);
    EXPECT_DOUBLE_EQ(result->daily.temperature_2m_min[0], 58.3);
}

// --- WeatherSource config parsing test ---

TEST(WeatherSourceConfig, DefaultIsGribStream) {
    TrackedSeries ts;
    EXPECT_EQ(ts.weather_source, WeatherSource::GribStream);
}
