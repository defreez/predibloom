# Open-Meteo API Documentation

This document explains the three Open-Meteo APIs and how they should be used in predibloom.

## The Three APIs

### 1. Archive API (Historical Weather)
- **Endpoint:** `archive-api.open-meteo.com`
- **What it returns:** Actual observed weather data (reanalysis)
- **Data source:** Weather stations, satellites, buoys, radar + gap-filling models
- **Use case:** Ground truth - what actually happened

This is what we use via `getHistoricalWeather()` to get actual temperatures for comparison.

### 2. Historical Forecast API
- **Endpoint:** `historical-forecast-api.open-meteo.com`
- **What it returns:** **RECONSTRUCTED** forecast data, NOT authentic historical forecasts
- **How it works:** "Constructed by continuously assembling weather forecasts, concatenating the first hours of each model update"
- **Data available:** 2021-2022 onwards (varies by model)

**CRITICAL MISUNDERSTANDING:** This API does NOT give you "the forecast as it existed on date X". It gives you a reconstructed continuous time series that "closely mirrors local measurements but provides global coverage."

**This is what our current backtest uses - AND IT'S WRONG for realistic backtesting.**

Source: https://open-meteo.com/en/docs/historical-forecast-api

### 3. Previous Runs API
- **Endpoint:** `previous-runs-api.open-meteo.com`
- **What it returns:** Actual archived forecasts from successive model runs
- **How it works:** Preserves what the forecast model predicted on different days
- **Data available:** January 2024+ for most models, April 2021+ for GFS

**This is what we SHOULD use for backtesting.**

The naming convention:
- `temperature_2m_previous_day0` = latest forecast (today's run)
- `temperature_2m_previous_day1` = forecast issued 24 hours ago
- `temperature_2m_previous_day2` = forecast issued 48 hours ago
- etc. (up to 7 days, extendable to 16)

Source: https://open-meteo.com/en/docs/previous-runs-api

## Current Implementation (BROKEN)

Current backtest in `src/cli/main.cpp` (lines 711-718):
```cpp
auto forecast_result = openmeteo.getHistoricalForecast(
    series_config->latitude, series_config->longitude,
    backtest_start, backtest_end);
```

This calls `historical-forecast-api.open-meteo.com` which returns reconstructed data.

**Why it doesn't hit 100% accuracy:** The reconstructed data is similar to but not identical to actual weather. It has inherent error because it's assembled from model runs, not pure observations.

**Why this is wrong:** We're not testing actual forecast skill. We're testing how well the reconstructed data matches NWS settlements, which is meaningless for predicting future trading performance.

## Correct Implementation (TODO)

For realistic backtesting, we need to:

1. **Add Previous Runs API support** to `OpenMeteoClient`
2. **Calculate lead time** for each trade (days between entry and settlement)
3. **Fetch forecast with appropriate lead time** using `temperature_2m_max_previous_dayN`

Example: If we're simulating entry on April 18 for settlement on April 20:
- Lead time = 2 days
- We need `temperature_2m_max_previous_day2` for the April 20 forecast

### API Call Example

```
GET https://previous-runs-api.open-meteo.com/v1/forecast
  ?latitude=40.71
  &longitude=-74.01
  &daily=temperature_2m_max,temperature_2m_max_previous_day1,temperature_2m_max_previous_day2
  &start_date=2026-04-20
  &end_date=2026-04-20
  &temperature_unit=fahrenheit
  &timezone=America/New_York
```

This returns:
- `temperature_2m_max` - today's forecast for April 20
- `temperature_2m_max_previous_day1` - yesterday's forecast for April 20
- `temperature_2m_max_previous_day2` - 2-days-ago forecast for April 20

## Data Availability Constraints

Previous Runs API data availability:
- Most models: January 2024 onwards
- GFS (what we likely want): April 2021 onwards for temperature_2m
- JMA: Back to 2018

This means we can only backtest with realistic forecasts from 2024 onwards for most data, or April 2021+ if we specifically use GFS.

Source: https://open-meteo.com/en/docs/previous-runs-api
