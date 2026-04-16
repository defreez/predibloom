# Kalshi API Reference

## Data Partitioning

Kalshi partitions data into **live** and **historical** tiers based on cutoff timestamps.

```bash
curl https://api.elections.kalshi.com/trade-api/v2/historical/cutoff
```

Returns:
```json
{
  "market_settled_ts": "2026-02-14T00:00:00Z",
  "trades_created_ts": "2026-02-14T00:00:00Z"
}
```

- Markets settled **before** cutoff → use `/historical/` endpoints
- Markets settled **after** cutoff → use live endpoints
- **Old Events and Series are always available through original endpoints**

## Endpoints

### Live Data (after cutoff)

| Endpoint | Description |
|----------|-------------|
| `GET /markets?series_ticker=X` | List markets for a series |
| `GET /markets/{ticker}` | Single market details |
| `GET /markets/trades?ticker=X` | Individual trades (has pre-settlement prices!) |
| `GET /series/{s}/markets/{t}/candlesticks` | OHLC data |

### Historical Data (before cutoff)

| Endpoint | Description |
|----------|-------------|
| `GET /historical/markets` | Archived markets |
| `GET /historical/trades` | Archived trades |
| `GET /historical/markets/{ticker}/candlesticks` | Archived OHLC |

## Getting Pre-Settlement Prices

The `last_price` field on a market is the **post-settlement** price (99¢ or 1¢). Useless for analysis.

To get pre-settlement prices, fetch trades:

```bash
curl "https://api.elections.kalshi.com/trade-api/v2/markets/trades?ticker=KXHIGHNY-26MAR10-T73&limit=500"
```

Returns individual trades with timestamps and prices:
```json
{
  "trades": [
    {
      "created_time": "2026-03-10T14:50:31Z",
      "yes_price_dollars": "0.7700",
      "count_fp": "20.00"
    }
  ]
}
```

To get a meaningful pre-settlement price:
1. Fetch all trades for the market
2. Filter to a time window (e.g., 9am on event day)
3. Compute VWAP or use last trade price

## Candlesticks

**IMPORTANT: Candlesticks are EMPTY for settled markets. Use trades instead.**

For live/open markets only:
```bash
curl "https://api.elections.kalshi.com/trade-api/v2/series/KXHIGHNY/markets/KXHIGHNY-26APR16-T91/candlesticks?period_interval=60&start_ts=START&end_ts=END"
```

Parameters:
- `period_interval`: 1 (minute), 60 (hour), 1440 (day)
- `start_ts` / `end_ts`: Unix timestamps

## Hourly Prices for Settled Markets (USE THIS)

Candlesticks don't work for settled markets. Fetch trades and aggregate by hour:

```bash
curl "https://api.elections.kalshi.com/trade-api/v2/markets/trades?ticker=KXHIGHNY-26MAR10-T73&limit=1000"
```

Then group by hour in code, or with jq:
```bash
curl ... | jq '[.trades[] | {ts: .created_time, price: .yes_price_dollars}] | group_by(.ts[:13]) | map({hour: .[0].ts[:13], price: .[0].price})'
```

Example output:
```json
[
  {"hour": "2026-03-10T12", "price": "0.8500"},
  {"hour": "2026-03-10T13", "price": "0.8000"},
  {"hour": "2026-03-10T14", "price": "0.8700"},
  {"hour": "2026-03-10T15", "price": "0.9800"},
  {"hour": "2026-03-10T16", "price": "0.9900"}
]
```

## KXHIGHNY (NYC High Temp) Markets

Ticker format: `KXHIGHNY-YYMMMDD-[B##]T##`

Examples:
- `KXHIGHNY-26APR10-T68` → Apr 10 2026, 68°F or higher
- `KXHIGHNY-26APR10-T61` → Apr 10 2026, below 61°F
- `KXHIGHNY-26APR10-B65.5` → Apr 10 2026, between 65-66°F

API fields:
- `floor_strike`: Lower bound (int or null)
- `cap_strike`: Upper bound (int or null)
- `result`: "yes" or "no" after settlement
