# Market vs Forecast Analysis: KXHIGHNY

Analysis of Kalshi NYC high temperature markets compared to Open-Meteo weather forecasts.

## Executive Summary

Using Open-Meteo forecasts with a calibration offset to predict Kalshi temperature market outcomes yields:

- **Win rate:** 91.7% (11/12 trades)
- **ROI:** 57-138% depending on entry timing
- **Edge:** Forecasts are accurate; markets underprice correct outcomes early

## Data Sources

| Purpose | Source | API |
|---------|--------|-----|
| Settlement (actual) | NWS Daily Climate Report | IEM: mesonet.agron.iastate.edu |
| Forecasts | Open-Meteo Historical Forecast | historical-forecast-api.open-meteo.com |
| Market prices | Kalshi Trades API | api.elections.kalshi.com |

### Critical: Temperature Source Mismatch

**Kalshi settles on NWS Central Park (KNYC)**, not Open-Meteo.

Open-Meteo uses ERA5 reanalysis (gridded model at ~25km resolution). NWS is the actual weather station observation. These differ by 1-4°F:

| Date | NWS (Settlement) | Open-Meteo | Difference |
|------|------------------|------------|------------|
| Mar 03 | 36°F | 34.2°F | +1.8°F |
| Mar 09 | 73°F | 69.8°F | +3.2°F |
| Mar 10 | 80°F | 75.9°F | +4.1°F |
| Mar 12 | 63°F | 64.7°F | -1.7°F |
| Mar 13 | 42°F | 41.7°F | +0.3°F |

**Solution:** Apply +2°F calibration offset to Open-Meteo forecasts.

## Trading Algorithm

```
1. Get Open-Meteo forecast for settlement date
2. Add calibration offset (+2°F default)
3. Find bracket that adjusted forecast falls into
4. Check margin: skip if forecast is < margin from bracket edge
5. Check price: skip if current price > max_price
6. BUY the bracket
7. Settlement: WIN if bracket settles YES, LOSS otherwise
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--offset` | +2°F | Calibration offset added to Open-Meteo forecast |
| `--margin` | 2-3°F | Min distance from bracket edge to trade |
| `--max-price` | 99¢ | Max price willing to pay |
| `--entry-hour` | 0-12 | Hour on settlement day to enter |

## Backtest Results

**Period:** Jan 1 - Apr 14, 2026 (65 trading days)
**Parameters:** offset=+2°F, margin=2°F, max_price=99¢

### Entry Timing Impact

| Entry Time | Avg Entry Price | ROI | Notes |
|------------|-----------------|-----|-------|
| Midnight (T00) | 38.5¢ | **138%** | Best prices, most uncertainty |
| 6am (T06) | 46.2¢ | 99% | Before temp data |
| Noon (T12) | 58.3¢ | 57% | Market has priced in most info |

Earlier entry = cheaper prices = higher ROI, but also betting before actual temperature data.

### Detailed Results (Entry at Midnight)

```
Date        Strike    Forecast Adjusted NWS   Entry Time     Price  Result P&L
----------------------------------------------------------------------------------------
2026-02-18  <44       39.9     41.9     41    2026-02-18T00  71.0   WIN    +29.0
2026-02-26  43+       43.2     45.2     49    2026-02-26T00  4.0    WIN    +96.0
2026-03-09  70+       72.3     74.3     73    2026-03-09T00  17.0   WIN    +83.0
2026-03-10  73+       78.7     80.7     80    2026-03-10T00  60.0   WIN    +40.0
2026-03-12  <64       58.8     60.8     63    2026-03-12T00  54.0   WIN    +46.0
2026-03-21  60+       60.2     62.2     59    2026-03-20T23  10.0   LOSS   -10.0
2026-03-23  <54       49.6     51.6     50    2026-03-23T00  89.0   WIN    +11.0
2026-03-26  72+       75.1     77.1     76    2026-03-26T00  58.0   WIN    +42.0
2026-03-30  69+       72.3     74.3     73    2026-03-30T00  11.0   WIN    +89.0
2026-03-31  77+       81.6     83.6     81    2026-03-31T00  71.0   WIN    +29.0
2026-04-07  55+       55.7     57.7     56    2026-04-07T00  4.0    WIN    +96.0
2026-04-14  86+       86.0     88.0     87    2026-04-14T00  13.0   WIN    +87.0
----------------------------------------------------------------------------------------

Summary:
  Trades: 12 (11 wins, 1 loss)
  Win rate: 91.7%
  Total P&L: +638¢ per contract
  Avg entry: 38.5¢
  ROI: +138.1%
```

### The Single Loss

**Mar 21:** Forecast 60.2°F → Adjusted 62.2°F → Predicted 60+ → NWS actual 59°F

The adjusted forecast was only 2.2°F above the 60°F threshold. With margin=2°F, this was right at the edge of our confidence threshold. The NWS came in at 59°F, just barely under.

## Key Insights

### 1. Forecast Accuracy is High

Open-Meteo forecasts (with offset) correctly predicted the winning bracket 91.7% of the time. The systematic bias between Open-Meteo and NWS is consistent enough to calibrate.

### 2. Markets Underprice Early

Price convergence pattern for winning brackets:

```
T-24h:  10-40¢  (underpriced)
T-12h:  30-60¢  (still underpriced)
T-6h:   50-80¢  (approaching fair value)
T-0:    99¢     (settled)
```

### 3. Entry Timing Matters

The earlier you enter, the better prices but more risk. Entering at midnight uses the previous day's closing price before any settlement day temperature data.

### 4. Selectivity is Key

Only 12 of 65 days (18%) met our criteria. The margin requirement filters out uncertain trades where the forecast is too close to a bracket boundary.

## Backtest Methodology Note

**Important:** This backtest uses the actual trade price at the specified entry hour, not the minimum price that ever traded. Earlier versions incorrectly cherry-picked the lowest price, which inflated ROI to unrealistic levels (~1000%).

The current methodology:
1. Find all trades for the target bracket
2. Select the most recent trade at or before the entry hour
3. Use that price as entry price

This is realistic: you would place a market order and get filled at the current price.

## CLI Usage

```bash
# Basic backtest
./predibloom-cli backtest --series KXHIGHNY --start 2026-01-01 --end 2026-04-14

# With custom parameters
./predibloom-cli backtest --series KXHIGHNY \
  --start 2026-01-01 --end 2026-04-14 \
  --offset 2.0 \
  --margin 2.0 \
  --max-price 99 \
  --entry-hour 0
```

## Next Steps

1. ~~Identify Kalshi's temperature source~~ Done: NWS CLI for KNYC
2. ~~Build NWS CLI client~~ Done
3. ~~Backtest with realistic entry prices~~ Done: 138% ROI
4. Expand to other cities (Miami, Chicago, etc.)
5. Add real-time trading signals
6. Paper trade forward to validate out-of-sample

## Risks and Caveats

1. **Small sample size:** 12 trades is not statistically significant
2. **Liquidity:** These markets are thin; large orders move prices
3. **Execution:** Real trading has slippage and fees
4. **Model drift:** Open-Meteo model updates could change forecast accuracy
5. **Overfitting:** Parameters tuned on this data may not generalize
