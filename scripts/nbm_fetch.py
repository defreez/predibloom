#!/usr/bin/env python3
"""
Fetch NBM (National Blend of Models) temperature forecasts from NOAA S3.

Downloads GRIB2 files from s3://noaa-nbm-grib2-pds/, extracts TMP@2m for a
given lat/lon coordinate, and outputs daily min/max temperatures as JSON.

Usage:
    nbm_fetch.py --lat 40.758 --lon -73.985 --date 2026-04-20 [--as-of 2026-04-19T04:00:00Z]

Output (JSON to stdout):
    {"date": "2026-04-20", "temp_max_f": 72.5, "temp_min_f": 58.3}

Dependencies:
    pip install s3fs xarray cfgrib ecmwflibs

NBM S3 path pattern:
    blend.{YYYYMMDD}/{HH}/core/blend.t{HH}z.core.f{FFF}.co.grib2

CONUS cycles: 01, 07, 13, 19 UTC
"""

import argparse
import hashlib
import json
import os
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional

# Check for required dependencies before import
try:
    import s3fs
    import xarray as xr
except ImportError as e:
    print(json.dumps({"error": f"Missing dependency: {e}. Install with: pip install s3fs xarray cfgrib ecmwflibs"}), file=sys.stderr)
    sys.exit(1)


# NBM model run cycles (UTC hours)
NBM_CYCLES = [1, 7, 13, 19]

# S3 bucket
NBM_BUCKET = "noaa-nbm-grib2-pds"

# Cache directory
CACHE_DIR = Path(os.getenv("NBM_CACHE_DIR", ".cache/nbm"))


def get_cache_path(date: str, cycle: int, lat: float, lon: float) -> Path:
    """Generate cache file path for a specific forecast."""
    # Round lat/lon to 3 decimals for cache key
    lat_str = f"{lat:.3f}"
    lon_str = f"{lon:.3f}"
    return CACHE_DIR / date / f"{cycle:02d}" / f"{lat_str}_{lon_str}.json"


def load_from_cache(date: str, cycle: int, lat: float, lon: float) -> Optional[dict]:
    """Load cached forecast if available."""
    cache_path = get_cache_path(date, cycle, lat, lon)
    if cache_path.exists():
        try:
            with open(cache_path) as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError):
            pass
    return None


def save_to_cache(date: str, cycle: int, lat: float, lon: float, data: dict):
    """Save forecast to cache."""
    cache_path = get_cache_path(date, cycle, lat, lon)
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    with open(cache_path, "w") as f:
        json.dump(data, f)


def find_best_cycle(target_date: str, as_of: Optional[datetime] = None) -> tuple[str, int]:
    """
    Find the best NBM cycle to use for forecasting the target date.

    Returns (cycle_date, cycle_hour) tuple.

    If as_of is provided, only considers cycles issued before that time.
    Otherwise, finds the most recent available cycle.
    """
    target = datetime.strptime(target_date, "%Y-%m-%d").replace(tzinfo=timezone.utc)

    if as_of is None:
        # Use the most recent cycle before target date midnight
        # NBM forecasts go out 10+ days, so use cycle from previous day evening
        cycle_dt = target - timedelta(days=1)
        cycle_hour = 19  # 19Z is the last cycle of the day
    else:
        # Find most recent cycle before as_of
        cycle_dt = as_of

        # Find the most recent cycle hour that's before as_of
        # Account for ~2hr delay from cycle time to data availability
        effective_hour = as_of.hour - 2

        if effective_hour < 1:
            cycle_dt = cycle_dt - timedelta(days=1)
            cycle_hour = 19
        elif effective_hour < 7:
            cycle_hour = 1
        elif effective_hour < 13:
            cycle_hour = 7
        elif effective_hour < 19:
            cycle_hour = 13
        else:
            cycle_hour = 19

    cycle_date = cycle_dt.strftime("%Y-%m-%d")
    return cycle_date, cycle_hour


def compute_forecast_hours(cycle_date: str, cycle_hour: int, target_date: str) -> list[int]:
    """
    Compute the forecast hours (f001, f002, etc.) needed to cover the target date
    in America/New_York local time.

    Returns list of forecast hours to fetch.
    """
    # Cycle datetime in UTC
    cycle_dt = datetime.strptime(f"{cycle_date}T{cycle_hour:02d}:00:00", "%Y-%m-%dT%H:%M:%S")
    cycle_dt = cycle_dt.replace(tzinfo=timezone.utc)

    # Target date midnight in NYC (approximate - we'll fetch a range)
    # NYC is UTC-5 in winter, UTC-4 in summer
    # For simplicity, use UTC-5 and fetch extra hours to be safe
    target_start = datetime.strptime(f"{target_date}T05:00:00", "%Y-%m-%dT%H:%M:%S")
    target_start = target_start.replace(tzinfo=timezone.utc)  # midnight NYC = 05:00 UTC

    target_end = target_start + timedelta(hours=24)

    # Calculate forecast hours
    hours = []
    for h in range(1, 265):  # NBM goes out to ~264 hours
        valid_time = cycle_dt + timedelta(hours=h)
        if target_start <= valid_time < target_end:
            hours.append(h)

    return hours


def fetch_nbm_temps(cycle_date: str, cycle_hour: int, forecast_hour: int,
                    lat: float, lon: float) -> Optional[float]:
    """
    Fetch temperature from a single NBM GRIB2 file.

    Returns temperature in Kelvin, or None if not available.
    """
    # Construct S3 path
    # blend.{YYYYMMDD}/{HH}/core/blend.t{HH}z.core.f{FFF}.co.grib2
    date_str = cycle_date.replace("-", "")
    s3_path = f"s3://{NBM_BUCKET}/blend.{date_str}/{cycle_hour:02d}/core/blend.t{cycle_hour:02d}z.core.f{forecast_hour:03d}.co.grib2"

    try:
        # Use anonymous S3 access
        fs = s3fs.S3FileSystem(anon=True)

        # Download to temp file and read with xarray/cfgrib
        import tempfile
        with tempfile.NamedTemporaryFile(suffix=".grib2", delete=False) as tmp:
            tmp_path = tmp.name

        try:
            fs.get(s3_path, tmp_path)

            # Open GRIB2 file with cfgrib
            # filter_by_keys to get TMP at 2m above ground
            ds = xr.open_dataset(
                tmp_path,
                engine="cfgrib",
                backend_kwargs={
                    "filter_by_keys": {
                        "typeOfLevel": "heightAboveGround",
                        "level": 2,
                        "shortName": "2t"  # 2m temperature
                    }
                }
            )

            # Extract temperature at nearest grid point
            # NBM uses latitude/longitude coordinates
            temp = ds["t2m"].sel(latitude=lat, longitude=lon, method="nearest").values

            return float(temp)

        finally:
            os.unlink(tmp_path)

    except Exception as e:
        print(f"Warning: Failed to fetch {s3_path}: {e}", file=sys.stderr)
        return None


def fetch_daily_temps(target_date: str, lat: float, lon: float,
                      as_of: Optional[datetime] = None) -> dict:
    """
    Fetch daily min/max temperatures for a target date.

    Returns dict with date, temp_max_f, temp_min_f.
    """
    # Find best cycle to use
    cycle_date, cycle_hour = find_best_cycle(target_date, as_of)

    # Check cache first
    cached = load_from_cache(target_date, cycle_hour, lat, lon)
    if cached:
        return cached

    # Compute forecast hours needed
    hours = compute_forecast_hours(cycle_date, cycle_hour, target_date)

    if not hours:
        return {"error": f"No forecast hours found for {target_date} from {cycle_date}T{cycle_hour:02d}Z"}

    # Fetch temperatures for each hour
    temps_k = []
    for fhr in hours:
        temp = fetch_nbm_temps(cycle_date, cycle_hour, fhr, lat, lon)
        if temp is not None:
            temps_k.append(temp)

    if not temps_k:
        return {"error": f"No temperature data retrieved for {target_date}"}

    # Convert Kelvin to Fahrenheit and compute min/max
    temps_f = [(t - 273.15) * 9/5 + 32 for t in temps_k]

    result = {
        "date": target_date,
        "temp_max_f": round(max(temps_f), 1),
        "temp_min_f": round(min(temps_f), 1),
        "cycle": f"{cycle_date}T{cycle_hour:02d}Z",
        "hours_fetched": len(temps_f)
    }

    # Save to cache
    save_to_cache(target_date, cycle_hour, lat, lon, result)

    return result


def parse_as_of(as_of_str: str) -> Optional[datetime]:
    """Parse ISO-8601 timestamp to datetime."""
    if not as_of_str:
        return None

    # Handle various ISO formats
    as_of_str = as_of_str.rstrip("Z")

    formats = [
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%dT%H:%M",
        "%Y-%m-%dT%H",
    ]

    for fmt in formats:
        try:
            dt = datetime.strptime(as_of_str, fmt)
            return dt.replace(tzinfo=timezone.utc)
        except ValueError:
            continue

    raise ValueError(f"Cannot parse as-of timestamp: {as_of_str}")


def main():
    parser = argparse.ArgumentParser(
        description="Fetch NBM temperature forecasts from NOAA S3"
    )
    parser.add_argument("--lat", type=float, required=True, help="Latitude")
    parser.add_argument("--lon", type=float, required=True, help="Longitude")
    parser.add_argument("--date", type=str, required=True, help="Target date (YYYY-MM-DD)")
    parser.add_argument("--as-of", type=str, default="", help="Point-in-time constraint (ISO-8601 UTC)")
    parser.add_argument("--cache-dir", type=str, help="Cache directory (default: .cache/nbm)")

    args = parser.parse_args()

    # Set cache directory
    global CACHE_DIR
    if args.cache_dir:
        CACHE_DIR = Path(args.cache_dir)

    # Parse as-of time
    as_of = None
    if args.as_of:
        try:
            as_of = parse_as_of(args.as_of)
        except ValueError as e:
            print(json.dumps({"error": str(e)}))
            sys.exit(1)

    # Validate date format
    try:
        datetime.strptime(args.date, "%Y-%m-%d")
    except ValueError:
        print(json.dumps({"error": f"Invalid date format: {args.date}. Use YYYY-MM-DD"}))
        sys.exit(1)

    # Fetch temperatures
    result = fetch_daily_temps(args.date, args.lat, args.lon, as_of)

    # Output JSON
    print(json.dumps(result))

    if "error" in result:
        sys.exit(1)


if __name__ == "__main__":
    main()
