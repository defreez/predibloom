# predibloom
Bloomberg-style terminal for prediction market APIs (Kalshi, Polymarket, etc.)

## Adding a New City (Temperature Markets)

To add a new city for high-temp predictions, you need four things:

1. **Kalshi series ticker** - Find the series on Kalshi for the city's daily high temperature market. The ticker follows the pattern `KXHIGH<CITY>` (e.g., `KXHIGHNY`, `KXHIGHLAX`, `KXHIGHDEN`).

2. **Coordinates** - Latitude and longitude for the city. Used to pull forecasts from the Open-Meteo API.

3. **NWS station code** - The ICAO station identifier used by the National Weather Service (e.g., `KJFK`, `KLAX`, `KORD`). This is used by the backtest and winners commands to fetch actual observed temperatures. You can look these up at https://www.weather.gov.

4. **Calibration offset** - Open-Meteo forecasts don't perfectly match the NWS observations that Kalshi uses for settlement. The offset (in °F) corrects for this bias. Start with `0.0` and calibrate using backtest (see below).

### Config Entry

Add the new series to a tab in `config.json` (repo root). This file is symlinked to `~/.config/predibloom/config.json`:

```json
{
  "series_ticker": "KXHIGHDEN",
  "label": "Denver High",
  "latitude": 39.7392,
  "longitude": -104.9903,
  "nws_station": "KDEN",
  "offset": 0.0
}
```

### Calibrating the Offset

The offset corrects for systematic bias between Open-Meteo forecasts and the NWS observations that Kalshi uses for settlement. To find it, compare Open-Meteo forecast predictions against NWS settled values over a recent period and calculate the average difference (NWS actual - Open-Meteo forecast). That average is your offset. Typical values range from -2.5 to +1.5.

### Verify

```bash
./build/predibloom-cli predict -d 2025-04-18 -s KXHIGHDEN --margin 1.5
```
