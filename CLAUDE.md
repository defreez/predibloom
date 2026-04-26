# Predibloom Project Instructions

## GUI Testing via MCP

The GUI can be tested via MCP tools that communicate over a Unix socket.

**Setup:**
1. User starts GUI with control socket: `./predibloom --control`
2. This creates `/tmp/predibloom.sock` for MCP communication
3. MCP server (`mcp-server/predibloom_mcp.py`) connects to this socket

**Available MCP tools:**

- `predibloom_screenshot` - Capture screenshot (raylib saves to cwd, so screenshots go to repo root, e.g. `screenshot.png`)
- `predibloom_click` - Simulate click at x,y coordinates
- `predibloom_scroll` - Scroll market list (positive=up, negative=down)
- `predibloom_get_state` - Get JSON state (num_markets, selected_market_idx, scroll_offset, etc.)

**Testing workflow:**
1. Ask user to start: `./build/predibloom --control`
2. Use `predibloom_get_state` to check connection and state
3. Use `predibloom_screenshot` to see current UI
4. Use Read tool to view the screenshot file


## Build Commands

```bash
cd build && cmake .. && make
```

## Project Structure

- `src/api/` - Kalshi API client
- `src/core/` - Service layer and config
- `src/gui/` - GUI application (raylib)
- `src/cli/` - CLI tool
- `src/ui/` - UI theme/styling
- `mcp-server/` - MCP server for GUI automation

## Config

Location: `~/.config/predibloom/config.json`

```json
{
  "tabs": [
    {
      "name": "Politics",
      "series": [
        {"series_ticker": "KXHORMUZNORM", "label": "Strait of Hormuz"}
      ]
    },
    {
      "name": "Climate",
      "series": [
        {"series_ticker": "KXHIGHNY", "label": "NYC High Temp"}
      ]
    }
  ]
}
```

The GUI shows tabs in the toolbar. Each tab contains one or more market series. Clicking a tab fetches markets for all series in that tab.

## Backtesting Fundamentals

**Critical constraint: Backtesting requires forecast data from BEFORE entry time.**

The backtest simulates what a trader would have known at entry time. This means:

- For a trade entered at time T, you need forecast data that was available at time T
- NBM cycles become available ~2 hours after their nominal time (19Z available ~21Z)
- The `entry_hour` config determines which cycle is used

**Example with entry_hour=0 (midnight UTC):**
- Settlement date: 2026-04-25
- Entry time: 2026-04-25T00:00:00Z
- Effective time (minus 2hr delay): 2026-04-24T22:00:00Z
- Selected cycle: 2026-04-24 19Z (the latest available at entry)

**Implication for data capture:**
- To backtest date X, you need the NBM cycle from the day BEFORE X (for evening entry times)
- Capturing today's cycles lets you backtest TOMORROW's markets (once they settle)
- NOAA S3 only keeps ~10 days of data, limiting historical backfills

**NBM cycle schedule (CONUS):** 01Z, 07Z, 13Z, 19Z daily

**Local NBM storage:**
```
~/.cache/predibloom/nbm/
├── grids/
│   └── blend.YYYYMMDD/
│       └── HH/               # 01, 07, 13, 19
│           ├── 2t.fFFF.nc    # extracted 2m temp (fast queries)
│           └── core.fFFF.grib2  # full GRIB2 (all variables)
└── index.db
```

## Trading Thesis

**Core idea:** Latency arbitrage on NBM data. It takes time for new forecast data to get priced into Kalshi weather markets.

**The edge:**

1. **Latency** - NBM becomes available ~2hr after cycle time on NOAA S3. Most traders wait for downstream products (weather.gov, Weather Channel, apps) that add more delay.

2. **Raw data** - Direct from S3, not filtered through someone else's interpretation.

3. **Custom heuristics** - Our calibration offsets, understanding of NWS station quirks, models for how NBM bias varies by location/season.

**Why keep full GRIB2:** Potential signal in variables others ignore - dewpoint affecting "feels like", wind affecting measurement site behavior, etc.

**Backtest purpose:** Not just validating accuracy - measuring how fast the edge decays. If still profitable entering 6 hours after a cycle vs 2 hours, the market is slow to price in new information.
