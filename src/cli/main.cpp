#include <CLI/CLI.hpp>
#include "../api/kalshi_client.hpp"
#include "../api/gribstream_client.hpp"
#include "../core/service.hpp"
#include "../core/config.hpp"
#include "../core/weather_comparison.hpp"
#include "formatters.hpp"
#include "commands/predict.hpp"
#include "commands/backtest.hpp"
#include "commands/calibrate.hpp"
#include "commands/portfolio.hpp"
#include "commands/misc.hpp"
#include "commands/nbm.hpp"
#include "commands/history.hpp"
#include <iostream>

int main(int argc, char** argv) {
    auto config = predibloom::core::Config::load();
    CLI::App app{"predibloom - Kalshi market viewer"};

    // Subcommands
    auto* markets_cmd = app.add_subcommand("markets", "List markets");
    auto* events_cmd = app.add_subcommand("events", "List events");
    auto* orderbook_cmd = app.add_subcommand("orderbook", "Show orderbook for a market");
    auto* market_cmd = app.add_subcommand("market", "Show details for a single market");
    auto* compare_cmd = app.add_subcommand("compare", "Compare Kalshi prices with weather data");
    auto* history_cmd = app.add_subcommand("history", "Hourly price history for all brackets");
    auto* winners_cmd = app.add_subcommand("winners", "Price history for settled brackets");
    auto* backtest_cmd = app.add_subcommand("backtest", "Backtest trading strategy");
    auto* predict_cmd = app.add_subcommand("predict", "Predict trade for a given day");
    auto* series_cmd = app.add_subcommand("series", "List configured series");
    auto* calibrate_cmd = app.add_subcommand("calibrate", "Calibrate forecast offset");
    auto* fills_cmd = app.add_subcommand("fills", "Show trade fills (requires auth)");
    auto* nbm_cmd = app.add_subcommand("nbm", "NBM weather data");
    auto* nbm_download_cmd  = nbm_cmd->add_subcommand("download",  "Bulk download per config");
    auto* nbm_list_cmd      = nbm_cmd->add_subcommand("list",      "List cached forecasts");
    auto* nbm_remote_cmd    = nbm_cmd->add_subcommand("remote",    "List S3 availability");
    auto* nbm_fetch_cmd     = nbm_cmd->add_subcommand("fetch",     "Ad-hoc single fetch");
    auto* nbm_inventory_cmd = nbm_cmd->add_subcommand("inventory", "GRIB2 variable inventory");
    auto* nbm_capture_cmd   = nbm_cmd->add_subcommand("capture",   "Capture full cycle to NetCDF4");
    auto* nbm_capture_missing_cmd = nbm_cmd->add_subcommand("capture-missing", "Download missing cycles");
    auto* nbm_cleanup_cmd   = nbm_cmd->add_subcommand("cleanup",   "Delete old grid files");
    auto* nbm_grids_cmd     = nbm_cmd->add_subcommand("grids",     "List captured grids");
    auto* portfolio_cmd = app.add_subcommand("portfolio", "Show portfolio (requires auth)");
    auto* portfolio_positions_cmd = portfolio_cmd->add_subcommand("positions", "Show open positions");
    auto* portfolio_settlements_cmd = portfolio_cmd->add_subcommand("settlements", "Show settlements");
    int portfolio_settle_days = 7;
    portfolio_settlements_cmd->add_option("-d,--days", portfolio_settle_days, "Days to look back")
        ->default_val(7);

    // Markets command options
    std::string markets_status = "open", markets_event_ticker, markets_series_ticker, markets_format = "table";
    int markets_limit = 10;
    markets_cmd->add_option("-s,--status", markets_status, "Filter by status")->default_val("open");
    markets_cmd->add_option("-e,--event", markets_event_ticker, "Filter by event ticker");
    markets_cmd->add_option("--series-ticker", markets_series_ticker, "Filter by series ticker");
    markets_cmd->add_option("-n,--limit", markets_limit, "Number of results")->default_val(10)->check(CLI::Range(1, 1000));
    markets_cmd->add_option("-f,--format", markets_format, "Output format")->default_val("table")->check(CLI::IsMember({"table", "json", "csv"}));

    // Events command options
    std::string events_status, events_format = "table";
    int events_limit = 10;
    events_cmd->add_option("-s,--status", events_status, "Filter by status");
    events_cmd->add_option("-n,--limit", events_limit, "Number of results")->default_val(10)->check(CLI::Range(1, 1000));
    events_cmd->add_option("-f,--format", events_format, "Output format")->default_val("table")->check(CLI::IsMember({"table", "json", "csv"}));

    // Orderbook command options
    std::string orderbook_ticker, orderbook_format = "table";
    int orderbook_depth = 10;
    orderbook_cmd->add_option("ticker", orderbook_ticker, "Market ticker")->required();
    orderbook_cmd->add_option("-d,--depth", orderbook_depth, "Depth")->default_val(10)->check(CLI::Range(1, 100));
    orderbook_cmd->add_option("-f,--format", orderbook_format, "Output format")->default_val("table")->check(CLI::IsMember({"table", "json", "csv"}));

    // Market detail command options
    std::string market_ticker, market_format = "table";
    market_cmd->add_option("ticker", market_ticker, "Market ticker")->required();
    market_cmd->add_option("-f,--format", market_format, "Output format")->default_val("table")->check(CLI::IsMember({"table", "json"}));

    // Compare command options
    std::string compare_series, compare_start, compare_end, compare_format = "table";
    compare_cmd->add_option("-s,--series", compare_series, "Series ticker")->required();
    compare_cmd->add_option("--start", compare_start, "Start date YYYY-MM-DD")->required();
    compare_cmd->add_option("--end", compare_end, "End date YYYY-MM-DD")->required();
    compare_cmd->add_option("-f,--format", compare_format, "Output format")->default_val("table")->check(CLI::IsMember({"table", "json", "csv"}));

    // History command options
    std::string history_series, history_start, history_end;
    history_cmd->add_option("-s,--series", history_series, "Series ticker")->required();
    history_cmd->add_option("--start", history_start, "Start date YYYY-MM-DD")->required();
    history_cmd->add_option("--end", history_end, "End date YYYY-MM-DD")->required();

    // Winners command options
    std::string winners_series, winners_start, winners_end;
    winners_cmd->add_option("-s,--series", winners_series, "Series ticker")->required();
    winners_cmd->add_option("--start", winners_start, "Start date YYYY-MM-DD")->required();
    winners_cmd->add_option("--end", winners_end, "End date YYYY-MM-DD")->required();

    // Backtest command options
    std::vector<std::string> backtest_series;
    std::string backtest_start, backtest_end, backtest_algo;
    double backtest_margin = 0.0, backtest_min_price = 5.0, backtest_max_price = 40.0, backtest_trade_size = 0.0;
    int backtest_entry_hour = -1, backtest_exit_hour = -1;
    int backtest_latency = 0;
    std::vector<int> backtest_latency_sweep;
    backtest_cmd->add_option("-s,--series", backtest_series, "Series ticker(s)")->delimiter(',');
    backtest_cmd->add_option("--start", backtest_start, "Start date YYYY-MM-DD")->required();
    backtest_cmd->add_option("--end", backtest_end, "End date YYYY-MM-DD")->required();
    backtest_cmd->add_option("--algo", backtest_algo, "Algorithm (simple, latency)")->default_val("")->check(CLI::IsMember({"", "simple", "latency"}));
    backtest_cmd->add_option("--margin", backtest_margin, "Min margin (°F)")->default_val(0.0);
    backtest_cmd->add_option("--min-price", backtest_min_price, "Min price (cents)")->default_val(5.0);
    backtest_cmd->add_option("--max-price", backtest_max_price, "Max price (cents)")->default_val(40.0);
    backtest_cmd->add_option("--trade-size", backtest_trade_size, "Dollars per trade")->default_val(0.0);
    backtest_cmd->add_option("--entry-hour", backtest_entry_hour, "Entry hour UTC")->default_val(-1)->check(CLI::Range(-1, 23));
    backtest_cmd->add_option("--exit-hour", backtest_exit_hour, "Exit hour UTC")->default_val(-1)->check(CLI::Range(-1, 23));
    backtest_cmd->add_option("--latency", backtest_latency, "Latency algo: hours after cycle")->default_val(0)->check(CLI::Range(0, 24));
    backtest_cmd->add_option("--latency-sweep", backtest_latency_sweep, "Latency algo: test multiple latencies")->delimiter(',');

    // Predict command options
    std::string predict_series, predict_date, predict_cycle;
    double predict_margin = 2.0, predict_min_price = 5.0, predict_max_price = 40.0;
    predict_cmd->add_option("-s,--series", predict_series, "Series ticker");
    predict_cmd->add_option("-d,--date", predict_date, "Date YYYY-MM-DD")->required();
    predict_cmd->add_option("--cycle", predict_cycle, "Forecast cycle (YYYY-MM-DDTHH, e.g. 2026-04-25T19)");
    predict_cmd->add_option("--margin", predict_margin, "Min margin (°F)")->default_val(2.0);
    predict_cmd->add_option("--min-price", predict_min_price, "Min price (cents)")->default_val(5.0);
    predict_cmd->add_option("--max-price", predict_max_price, "Max price (cents)")->default_val(40.0);

    // Calibrate command options
    std::vector<std::string> calibrate_series;
    std::string calibrate_start, calibrate_end;
    int calibrate_entry_hour = -1;
    calibrate_cmd->add_option("-s,--series", calibrate_series, "Series ticker(s)")->delimiter(',');
    calibrate_cmd->add_option("--start", calibrate_start, "Start date YYYY-MM-DD")->required();
    calibrate_cmd->add_option("--end", calibrate_end, "End date YYYY-MM-DD")->required();
    calibrate_cmd->add_option("--entry-hour", calibrate_entry_hour, "Entry hour UTC");

    // Fills command options
    std::string fills_ticker, fills_format = "table";
    int fills_limit = 100;
    fills_cmd->add_option("-t,--ticker", fills_ticker, "Filter by ticker");
    fills_cmd->add_option("-n,--limit", fills_limit, "Number of results")->default_val(100)->check(CLI::Range(1, 1000));
    fills_cmd->add_option("-f,--format", fills_format, "Output format")->default_val("table")->check(CLI::IsMember({"table", "json", "csv"}));

    // NBM subcommand options
    std::string nbm_start, nbm_end;
    nbm_download_cmd->add_option("--start", nbm_start, "Start date YYYY-MM-DD")->required();
    nbm_download_cmd->add_option("--end", nbm_end, "End date YYYY-MM-DD")->required();

    std::string nbm_list_date, nbm_list_lat, nbm_list_lon, nbm_list_format = "table";
    nbm_list_cmd->add_option("--date", nbm_list_date, "Filter by date YYYY-MM-DD");
    nbm_list_cmd->add_option("--lat", nbm_list_lat, "Filter by latitude");
    nbm_list_cmd->add_option("--lon", nbm_list_lon, "Filter by longitude");
    nbm_list_cmd->add_option("-f,--format", nbm_list_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    std::string nbm_remote_date, nbm_remote_format = "table";
    int nbm_remote_days = 10;
    auto* nbm_remote_date_opt = nbm_remote_cmd->add_option("--date", nbm_remote_date,
        "List only this date YYYY-MM-DD");
    nbm_remote_cmd->add_option("--days", nbm_remote_days,
        "Number of most-recent days to list (default 10)")
        ->excludes(nbm_remote_date_opt);
    nbm_remote_cmd->add_option("-f,--format", nbm_remote_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    double nbm_fetch_lat = 0, nbm_fetch_lon = 0;
    std::string nbm_fetch_date, nbm_fetch_asof;
    bool nbm_fetch_force = false;
    nbm_fetch_cmd->add_option("--lat", nbm_fetch_lat, "Latitude")->required();
    nbm_fetch_cmd->add_option("--lon", nbm_fetch_lon, "Longitude")->required();
    nbm_fetch_cmd->add_option("--date", nbm_fetch_date, "Target date YYYY-MM-DD")->required();
    nbm_fetch_cmd->add_option("--as-of", nbm_fetch_asof, "Point-in-time constraint (ISO-8601 UTC)");
    nbm_fetch_cmd->add_flag("--force", nbm_fetch_force, "Bypass cache (re-download)");

    std::string nbm_inv_date, nbm_inv_format = "table";
    int nbm_inv_cycle = 19, nbm_inv_fhr = 1;
    nbm_inventory_cmd->add_option("--date", nbm_inv_date, "Cycle date YYYY-MM-DD")->required();
    nbm_inventory_cmd->add_option("--cycle", nbm_inv_cycle, "Cycle hour (1, 7, 13, or 19)")
        ->required()->check(CLI::IsMember({1, 7, 13, 19}));
    nbm_inventory_cmd->add_option("--forecast-hour", nbm_inv_fhr, "Forecast hour (default 1)")
        ->default_val(1);
    nbm_inventory_cmd->add_option("-f,--format", nbm_inv_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    // nbm capture options
    std::string nbm_capture_date, nbm_capture_fhrs, nbm_capture_format = "table";
    int nbm_capture_cycle = -1;
    nbm_capture_cmd->add_option("--date", nbm_capture_date, "Cycle date YYYY-MM-DD")->required();
    nbm_capture_cmd->add_option("--cycle", nbm_capture_cycle, "Cycle hour (1, 7, 13, or 19)")
        ->check(CLI::IsMember({1, 7, 13, 19}));
    nbm_capture_cmd->add_option("--forecast-hours", nbm_capture_fhrs,
        "Forecast hours: '1-264' or '1,2,3'. Default: all");
    nbm_capture_cmd->add_option("-f,--format", nbm_capture_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    // nbm capture-missing options
    int nbm_capture_missing_days = 10;
    std::string nbm_capture_missing_format = "table";
    nbm_capture_missing_cmd->add_option("--days", nbm_capture_missing_days,
        "How many days back to scan S3 (default 10)")->default_val(10);
    nbm_capture_missing_cmd->add_option("-f,--format", nbm_capture_missing_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    // nbm cleanup options
    int nbm_cleanup_older_than = 30;
    std::string nbm_cleanup_format = "table";
    nbm_cleanup_cmd->add_option("--older-than", nbm_cleanup_older_than,
        "Delete files older than N days (default 30)")->default_val(30);
    nbm_cleanup_cmd->add_option("-f,--format", nbm_cleanup_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    // nbm grids options
    std::string nbm_grids_format = "table";
    nbm_grids_cmd->add_option("-f,--format", nbm_grids_format, "Output format")
        ->default_val("table")->check(CLI::IsMember({"table", "json"}));

    app.require_subcommand(1);
    CLI11_PARSE(app, argc, argv);

    predibloom::api::KalshiClient client;
    predibloom::core::MarketService service(client);

    // Handle markets command
    if (*markets_cmd) {
        predibloom::core::MarketFilter filter;
        filter.status = markets_status;
        if (!markets_event_ticker.empty()) filter.event_ticker = markets_event_ticker;
        if (!markets_series_ticker.empty()) filter.series_ticker = markets_series_ticker;
        filter.limit = markets_limit;
        auto result = service.listMarkets(filter);
        if (!result.ok()) { std::cerr << "Error: " << result.error().message << "\n"; return 1; }
        predibloom::cli::printMarkets(result.value(), predibloom::cli::parseFormat(markets_format));
    }

    // Handle events command
    if (*events_cmd) {
        predibloom::core::EventFilter filter;
        if (!events_status.empty()) filter.status = events_status;
        filter.limit = events_limit;
        auto result = service.listEvents(filter);
        if (!result.ok()) { std::cerr << "Error: " << result.error().message << "\n"; return 1; }
        predibloom::cli::printEvents(result.value(), predibloom::cli::parseFormat(events_format));
    }

    // Handle orderbook command
    if (*orderbook_cmd) {
        auto result = service.getOrderbook(orderbook_ticker, orderbook_depth);
        if (!result.ok()) { std::cerr << "Error: " << result.error().message << "\n"; return 1; }
        predibloom::cli::printOrderbook(result.value(), predibloom::cli::parseFormat(orderbook_format));
    }

    // Handle market detail command
    if (*market_cmd) {
        auto result = service.getMarket(market_ticker);
        if (!result.ok()) { std::cerr << "Error: " << result.error().message << "\n"; return 1; }
        predibloom::cli::printMarketDetail(result.value(), predibloom::cli::parseFormat(market_format));
    }

    // Handle compare command
    if (*compare_cmd) {
        auto* series_config = config.findSeries(compare_series);
        if (!series_config || series_config->latitude == 0) {
            std::cerr << "Series not configured: " << compare_series << "\n";
            return 1;
        }
        if (!config.hasGribstream()) {
            std::cerr << "GribStream API token not configured\n";
            return 1;
        }
        predibloom::api::GribStreamClient gribstream(config.gribstream_api_token);
        gribstream.setCaching(true);
        predibloom::core::WeatherComparisonService comparison(client, gribstream);
        comparison.setLocation(series_config->latitude, series_config->longitude,
                               series_config->isLowTemp(),
                               series_config->entry_day_offset,
                               series_config->effectiveEntryHour());
        auto result = comparison.analyze(compare_series, compare_start, compare_end);
        if (!result.ok()) { std::cerr << "Error: " << result.error().message << "\n"; return 1; }
        predibloom::cli::printComparison(result.value(), predibloom::cli::parseFormat(compare_format));
    }

    // Handle history command
    if (*history_cmd) {
        return predibloom::cli::runHistory(history_series, history_start, history_end, client);
    }

    // Handle winners command
    if (*winners_cmd) {
        return predibloom::cli::runWinners(winners_series, winners_start, winners_end, config, client);
    }

    // Handle backtest command
    if (*backtest_cmd) {
        predibloom::cli::BacktestOptions opts;
        opts.series = backtest_series;
        opts.start_date = backtest_start;
        opts.end_date = backtest_end;
        opts.algo = backtest_algo;
        opts.margin = backtest_margin;
        opts.min_price = backtest_min_price;
        opts.max_price = backtest_max_price;
        opts.entry_hour = backtest_entry_hour;
        opts.exit_hour = backtest_exit_hour;
        opts.trade_size = backtest_trade_size;
        opts.latency_hours = backtest_latency;
        opts.latency_sweep = backtest_latency_sweep;
        return predibloom::cli::runBacktest(opts, config, client);
    }

    // Handle predict command
    if (*predict_cmd) {
        predibloom::cli::PredictOptions opts;
        opts.series = predict_series;
        opts.date = predict_date;
        opts.cycle = predict_cycle;
        opts.margin = predict_margin;
        opts.min_price = predict_min_price;
        opts.max_price = predict_max_price;
        return predibloom::cli::runPredict(opts, config, client);
    }

    // Handle calibrate command
    if (*calibrate_cmd) {
        predibloom::cli::CalibrateOptions opts;
        opts.series = calibrate_series;
        opts.start_date = calibrate_start;
        opts.end_date = calibrate_end;
        opts.entry_hour = calibrate_entry_hour;
        return predibloom::cli::runCalibrate(opts, config);
    }

    // Handle fills command
    if (*fills_cmd) {
        return predibloom::cli::runFills(config, client, fills_ticker, fills_limit, fills_format);
    }

    // Handle portfolio command
    if (*portfolio_cmd) {
        if (*portfolio_positions_cmd) return predibloom::cli::runPortfolioPositions(config, client);
        if (*portfolio_settlements_cmd) return predibloom::cli::runPortfolioSettlements(config, client, portfolio_settle_days);
        return predibloom::cli::runPortfolioBalance(config, client);
    }

    // Handle nbm subcommand
    if (*nbm_cmd) {
        if (*nbm_download_cmd)  return predibloom::cli::runNbmDownload(config, nbm_start, nbm_end);
        if (*nbm_list_cmd)      return predibloom::cli::runNbmList(nbm_list_date, nbm_list_lat, nbm_list_lon, nbm_list_format);
        if (*nbm_remote_cmd)    return predibloom::cli::runNbmRemote(nbm_remote_date, nbm_remote_days, "", nbm_remote_format);
        if (*nbm_fetch_cmd)     return predibloom::cli::runNbmFetch(nbm_fetch_lat, nbm_fetch_lon, nbm_fetch_date, nbm_fetch_asof, nbm_fetch_force);
        if (*nbm_inventory_cmd) return predibloom::cli::runNbmInventory(nbm_inv_date, nbm_inv_cycle, nbm_inv_fhr, nbm_inv_format);
        if (*nbm_capture_cmd) return predibloom::cli::runNbmCapture(nbm_capture_date, nbm_capture_cycle, nbm_capture_fhrs, nbm_capture_format);
        if (*nbm_capture_missing_cmd) return predibloom::cli::runNbmCaptureMissing(nbm_capture_missing_days, nbm_capture_missing_format);
        if (*nbm_cleanup_cmd) return predibloom::cli::runNbmCleanup(nbm_cleanup_older_than, nbm_cleanup_format);
        if (*nbm_grids_cmd) return predibloom::cli::runNbmGrids(nbm_grids_format);
        std::cerr << "nbm requires a subcommand (download|list|remote|fetch|inventory|capture|capture-missing|cleanup|grids)\n";
        return 1;
    }

    // Handle series command
    if (*series_cmd) {
        return predibloom::cli::runSeries(config);
    }

    return 0;
}
