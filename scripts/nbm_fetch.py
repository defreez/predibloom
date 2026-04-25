#!/usr/bin/env python3
"""
NBM (National Blend of Models) data utility.

Subcommands:
    fetch        Fetch daily min/max temps for a target date (JSON).
    list-cache   List locally-cached forecasts (JSON).
    list-remote  List cycles available on NOAA S3 (JSON).
    inventory    List GRIB2 variables in a single NBM file (JSON).

Legacy invocation (without a subcommand) is treated as `fetch` for
backward compatibility with existing C++ callers.

Dependencies:
    pip install s3fs xarray cfgrib ecmwflibs
    # inventory: prefers `pygrib`, falls back to the `grib_ls` binary
    # that ships with `ecmwflibs`.

NBM S3 path pattern:
    s3://noaa-nbm-grib2-pds/blend.{YYYYMMDD}/{HH}/core/blend.t{HH}z.core.f{FFF}.co.grib2

CONUS cycles: 01, 07, 13, 19 UTC.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

def _print_table(rows: list[dict], columns: list[str], headers: Optional[list[str]] = None):
    """Print rows as an aligned table."""
    if not rows:
        print("(no data)")
        return
    headers = headers or columns
    widths = [len(h) for h in headers]
    for row in rows:
        for i, col in enumerate(columns):
            val = str(row.get(col, ""))
            widths[i] = max(widths[i], len(val))

    header_line = "  ".join(h.ljust(widths[i]) for i, h in enumerate(headers))
    print(header_line)
    print("  ".join("-" * w for w in widths))
    for row in rows:
        line = "  ".join(str(row.get(col, "")).ljust(widths[i]) for i, col in enumerate(columns))
        print(line)


def _output(data, fmt: str, table_columns: list[str] = None, table_headers: list[str] = None):
    """Output data as JSON or table."""
    if fmt == "json":
        print(json.dumps(data))
    elif fmt == "table":
        if isinstance(data, dict):
            if "error" in data:
                print(f"Error: {data['error']}")
            else:
                for k, v in data.items():
                    print(f"{k}: {v}")
        elif isinstance(data, list):
            _print_table(data, table_columns or list(data[0].keys()) if data else [], table_headers)
        else:
            print(data)


# Heavy deps (s3fs, xarray, cfgrib, pygrib) are imported lazily inside the
# functions that need them. list-cache works with only the stdlib, so this keeps
# it usable in environments where the S3/GRIB stack isn't installed.

_S3_DEP_HINT = (
    "Missing dependency: pip install s3fs xarray cfgrib ecmwflibs"
)


def _require_s3fs():
    try:
        import s3fs  # noqa: F401
        return s3fs
    except ImportError as e:
        raise RuntimeError(f"{_S3_DEP_HINT} ({e})")


def _require_xarray():
    try:
        import xarray as xr
        return xr
    except ImportError as e:
        raise RuntimeError(f"{_S3_DEP_HINT} ({e})")


NBM_CYCLES = [1, 7, 13, 19]
NBM_BUCKET = "noaa-nbm-grib2-pds"

DEFAULT_CACHE_DIR = Path(os.getenv("NBM_CACHE_DIR", str(Path.home() / ".cache/predibloom/nbm")))
DEFAULT_GRIB_CACHE_DIR = DEFAULT_CACHE_DIR / "grib2"

# Module-level pointer used by fetch_daily_temps() and friends; mutated by the
# fetch command handler when --cache-dir is supplied.
CACHE_DIR = DEFAULT_CACHE_DIR


def get_cache_path(date: str, cycle: int, lat: float, lon: float, cache_dir: Path = None) -> Path:
    """Generate cache file path for a specific forecast."""
    base = cache_dir if cache_dir is not None else CACHE_DIR
    lat_str = f"{lat:.3f}"
    lon_str = f"{lon:.3f}"
    return base / date / f"{cycle:02d}" / f"{lat_str}_{lon_str}.json"


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
    """
    target = datetime.strptime(target_date, "%Y-%m-%d").replace(tzinfo=timezone.utc)

    if as_of is None:
        cycle_dt = target - timedelta(days=1)
        cycle_hour = 19
    else:
        cycle_dt = as_of
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
    """Forecast hours (f001, f002, ...) needed to cover target date in NYC local time."""
    cycle_dt = datetime.strptime(f"{cycle_date}T{cycle_hour:02d}:00:00", "%Y-%m-%dT%H:%M:%S")
    cycle_dt = cycle_dt.replace(tzinfo=timezone.utc)

    # NYC midnight ≈ 05:00 UTC (standard time). Fetch a 24-hour window.
    target_start = datetime.strptime(f"{target_date}T05:00:00", "%Y-%m-%dT%H:%M:%S")
    target_start = target_start.replace(tzinfo=timezone.utc)
    target_end = target_start + timedelta(hours=24)

    hours = []
    for h in range(1, 265):
        valid_time = cycle_dt + timedelta(hours=h)
        if target_start <= valid_time < target_end:
            hours.append(h)
    return hours


def s3_grib_path(cycle_date: str, cycle_hour: int, forecast_hour: int) -> str:
    """Return the full s3:// URL for a given NBM GRIB2 file."""
    date_str = cycle_date.replace("-", "")
    return (
        f"s3://{NBM_BUCKET}/blend.{date_str}/{cycle_hour:02d}"
        f"/core/blend.t{cycle_hour:02d}z.core.f{forecast_hour:03d}.co.grib2"
    )


def fetch_nbm_temps(cycle_date: str, cycle_hour: int, forecast_hour: int,
                    lat: float, lon: float) -> Optional[float]:
    """Fetch 2m temperature (Kelvin) from a single NBM GRIB2 file, or None on failure."""
    import numpy as np

    try:
        import pygrib
    except ImportError as e:
        print(f"Warning: pygrib not installed: {e}", file=sys.stderr)
        return None

    s3_path = s3_grib_path(cycle_date, cycle_hour, forecast_hour)
    try:
        s3fs_mod = _require_s3fs()
        fs = s3fs_mod.S3FileSystem(anon=True)
        with tempfile.NamedTemporaryFile(suffix=".grib2", delete=False) as tmp:
            tmp_path = tmp.name

        try:
            fs.get(s3_path, tmp_path)
            gribs = pygrib.open(tmp_path)
            for msg in gribs:
                if (msg.shortName == "2t" and
                    msg.typeOfLevel == "heightAboveGround" and
                    msg.level == 2):
                    data = msg.values
                    lats, lons = msg.latlons()
                    dist = np.sqrt((lats - lat)**2 + (lons - lon)**2)
                    idx = np.unravel_index(np.argmin(dist), dist.shape)
                    return float(data[idx])
            return None
        finally:
            os.unlink(tmp_path)
    except Exception as e:
        print(f"Warning: Failed to fetch {s3_path}: {e}", file=sys.stderr)
        return None


def fetch_daily_temps(target_date: str, lat: float, lon: float,
                      as_of: Optional[datetime] = None,
                      use_cache: bool = True) -> dict:
    """Fetch daily min/max temperatures for a target date."""
    cycle_date, cycle_hour = find_best_cycle(target_date, as_of)

    if use_cache:
        cached = load_from_cache(target_date, cycle_hour, lat, lon)
        if cached:
            return cached

    hours = compute_forecast_hours(cycle_date, cycle_hour, target_date)
    if not hours:
        return {"error": f"No forecast hours found for {target_date} from {cycle_date}T{cycle_hour:02d}Z"}

    temps_k = []
    for fhr in hours:
        temp = fetch_nbm_temps(cycle_date, cycle_hour, fhr, lat, lon)
        if temp is not None:
            temps_k.append(temp)

    if not temps_k:
        return {"error": f"No temperature data retrieved for {target_date}"}

    temps_f = [(t - 273.15) * 9/5 + 32 for t in temps_k]

    result = {
        "date": target_date,
        "temp_max_f": round(max(temps_f), 1),
        "temp_min_f": round(min(temps_f), 1),
        "cycle": f"{cycle_date}T{cycle_hour:02d}Z",
        "hours_fetched": len(temps_f),
    }

    save_to_cache(target_date, cycle_hour, lat, lon, result)
    return result


def parse_as_of(as_of_str: str) -> Optional[datetime]:
    """Parse ISO-8601 timestamp to datetime."""
    if not as_of_str:
        return None
    as_of_str = as_of_str.rstrip("Z")
    for fmt in ("%Y-%m-%dT%H:%M:%S", "%Y-%m-%dT%H:%M", "%Y-%m-%dT%H"):
        try:
            return datetime.strptime(as_of_str, fmt).replace(tzinfo=timezone.utc)
        except ValueError:
            continue
    raise ValueError(f"Cannot parse as-of timestamp: {as_of_str}")


# ---------------------------------------------------------------------------
# list-cache
# ---------------------------------------------------------------------------

_CACHE_FILE_RE = re.compile(r"^(?P<lat>-?\d+\.\d+)_(?P<lon>-?\d+\.\d+)\.json$")


def list_cache(cache_dir: Path, date: Optional[str] = None,
               lat: Optional[float] = None, lon: Optional[float] = None) -> list[dict]:
    """Walk cache_dir/{date}/{cycle}/{lat}_{lon}.json and return parsed entries."""
    if not cache_dir.exists():
        return []

    rows: list[dict] = []
    date_dirs = [cache_dir / date] if date else sorted(p for p in cache_dir.iterdir() if p.is_dir())
    for date_dir in date_dirs:
        if not date_dir.is_dir():
            continue
        ds = date_dir.name
        # Skip non-date dirs (e.g. grib2/)
        try:
            datetime.strptime(ds, "%Y-%m-%d")
        except ValueError:
            continue
        for cycle_dir in sorted(p for p in date_dir.iterdir() if p.is_dir()):
            try:
                cycle = int(cycle_dir.name)
            except ValueError:
                continue
            for f in sorted(cycle_dir.glob("*.json")):
                m = _CACHE_FILE_RE.match(f.name)
                if not m:
                    continue
                f_lat = float(m.group("lat"))
                f_lon = float(m.group("lon"))
                if lat is not None and abs(f_lat - lat) > 1e-3:
                    continue
                if lon is not None and abs(f_lon - lon) > 1e-3:
                    continue
                try:
                    with open(f) as fh:
                        data = json.load(fh)
                except (json.JSONDecodeError, IOError):
                    continue
                rows.append({
                    "date": ds,
                    "cycle": cycle,
                    "lat": f_lat,
                    "lon": f_lon,
                    "temp_max_f": data.get("temp_max_f"),
                    "temp_min_f": data.get("temp_min_f"),
                    "hours_fetched": data.get("hours_fetched"),
                    "path": str(f),
                })
    return rows


# ---------------------------------------------------------------------------
# list-remote
# ---------------------------------------------------------------------------

_BLEND_DIR_RE = re.compile(r"blend\.(?P<date>\d{8})$")


def _format_date(yyyymmdd: str) -> str:
    return f"{yyyymmdd[0:4]}-{yyyymmdd[4:6]}-{yyyymmdd[6:8]}"


def list_remote(days: Optional[int] = None, date: Optional[str] = None) -> list[dict]:
    """List available (date, cycle) pairs on NOAA S3.

    If `date` is given, list only that YYYY-MM-DD. Otherwise, list the most
    recent `days` (default 10) of `blend.YYYYMMDD/` directories.
    """
    s3fs_mod = _require_s3fs()
    fs = s3fs_mod.S3FileSystem(anon=True)

    if date:
        target = date.replace("-", "")
        blend_dirs = [f"{NBM_BUCKET}/blend.{target}"]
    else:
        top = fs.ls(NBM_BUCKET)
        candidates: list[tuple[str, str]] = []
        for entry in top:
            leaf = entry.rsplit("/", 1)[-1]
            m = _BLEND_DIR_RE.match(leaf)
            if m:
                candidates.append((m.group("date"), entry))
        candidates.sort(reverse=True)
        n = days if days is not None else 10
        blend_dirs = [e for _, e in candidates[:n]]

    rows: list[dict] = []
    for blend_dir in blend_dirs:
        leaf = blend_dir.rsplit("/", 1)[-1]
        m = _BLEND_DIR_RE.match(leaf)
        if not m:
            continue
        ds = _format_date(m.group("date"))
        try:
            cycle_entries = fs.ls(blend_dir)
        except FileNotFoundError:
            continue
        for ce in cycle_entries:
            cycle_leaf = ce.rsplit("/", 1)[-1]
            try:
                cycle = int(cycle_leaf)
            except ValueError:
                continue
            if cycle not in NBM_CYCLES:
                continue
            rows.append({
                "date": ds,
                "cycle": cycle,
                "s3_prefix": f"s3://{ce}/core/",
            })
    rows.sort(key=lambda r: (r["date"], r["cycle"]))
    return rows


# ---------------------------------------------------------------------------
# inventory
# ---------------------------------------------------------------------------

def _download_grib2(cycle_date: str, cycle_hour: int, forecast_hour: int,
                    grib_cache_dir: Path) -> Path:
    """Download a single GRIB2 file into grib_cache_dir, skipping if already present."""
    date_str = cycle_date.replace("-", "")
    out_dir = grib_cache_dir / f"blend.{date_str}" / f"{cycle_hour:02d}"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"f{forecast_hour:03d}.grib2"
    if out_path.exists() and out_path.stat().st_size > 0:
        return out_path

    s3_path = s3_grib_path(cycle_date, cycle_hour, forecast_hour)
    s3fs_mod = _require_s3fs()
    fs = s3fs_mod.S3FileSystem(anon=True)
    fs.get(s3_path, str(out_path))
    return out_path


def grib_inventory(cycle_date: str, cycle_hour: int, forecast_hour: int,
                   grib_cache_dir: Path) -> list[dict]:
    """Return GRIB2 message inventory for one file.

    Prefer pygrib; fall back to `grib_ls` (ecCodes, ships with ecmwflibs).
    """
    grib_path = _download_grib2(cycle_date, cycle_hour, forecast_hour, grib_cache_dir)

    try:
        import pygrib  # type: ignore
    except ImportError:
        pygrib = None

    if pygrib is not None:
        rows: list[dict] = []
        with pygrib.open(str(grib_path)) as gribs:
            for msg in gribs:
                rows.append({
                    "shortName": getattr(msg, "shortName", None),
                    "typeOfLevel": getattr(msg, "typeOfLevel", None),
                    "level": getattr(msg, "level", None),
                    "name": getattr(msg, "name", None),
                })
        return rows

    grib_ls = shutil.which("grib_ls")
    if grib_ls is None:
        raise RuntimeError(
            "GRIB inventory requires `pygrib` or the `grib_ls` binary "
            "(install pygrib or ensure ecmwflibs provides grib_ls on PATH)."
        )

    proc = subprocess.run(
        [grib_ls, "-p", "shortName,typeOfLevel,level,name", str(grib_path)],
        check=True, capture_output=True, text=True,
    )
    return _parse_grib_ls(proc.stdout)


def _parse_grib_ls(text: str) -> list[dict]:
    """Parse `grib_ls -p shortName,typeOfLevel,level,name` stdout."""
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if len(lines) < 2:
        return []
    # First line: filename header. Second line: column headers. Data follows.
    # Trailer like "N of N messages in N files" terminates data.
    headers_idx = None
    for i, ln in enumerate(lines):
        if ln.startswith("shortName"):
            headers_idx = i
            break
    if headers_idx is None:
        return []
    headers = lines[headers_idx].split()
    rows: list[dict] = []
    for ln in lines[headers_idx + 1:]:
        if "messages in" in ln:
            break
        parts = ln.split()
        if len(parts) < len(headers):
            continue
        # `name` may contain spaces; join trailing tokens.
        head_no_name = [h for h in headers if h != "name"]
        fixed = parts[:len(head_no_name)]
        name = " ".join(parts[len(head_no_name):]) if "name" in headers else None
        row = dict(zip(head_no_name, fixed))
        if name is not None:
            row["name"] = name
        # Coerce level to int where possible.
        if "level" in row:
            try:
                row["level"] = int(row["level"])
            except ValueError:
                pass
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# Command dispatch
# ---------------------------------------------------------------------------

def cmd_fetch(args: argparse.Namespace) -> int:
    global CACHE_DIR
    if args.cache_dir:
        CACHE_DIR = Path(args.cache_dir)

    as_of = None
    if args.as_of:
        try:
            as_of = parse_as_of(args.as_of)
        except ValueError as e:
            _output({"error": str(e)}, args.format)
            return 1

    try:
        datetime.strptime(args.date, "%Y-%m-%d")
    except ValueError:
        _output({"error": f"Invalid date format: {args.date}. Use YYYY-MM-DD"}, args.format)
        return 1

    result = fetch_daily_temps(args.date, args.lat, args.lon, as_of,
                               use_cache=not args.no_cache)
    _output(result, args.format)
    return 1 if "error" in result else 0


def cmd_list_cache(args: argparse.Namespace) -> int:
    cache_dir = Path(args.cache_dir) if args.cache_dir else CACHE_DIR
    rows = list_cache(cache_dir, date=args.date, lat=args.lat, lon=args.lon)
    _output(rows, args.format,
            ["date", "cycle", "lat", "lon", "temp_min_f", "temp_max_f", "hours_fetched"],
            ["Date", "Cycle", "Lat", "Lon", "Min F", "Max F", "Hours"])
    return 0


def cmd_list_remote(args: argparse.Namespace) -> int:
    try:
        rows = list_remote(days=args.days, date=args.date)
    except Exception as e:
        _output({"error": f"S3 listing failed: {e}"}, args.format)
        return 1
    _output(rows, args.format,
            ["date", "cycle", "s3_prefix"],
            ["Date", "Cycle", "S3 Prefix"])
    return 0


def cmd_inventory(args: argparse.Namespace) -> int:
    grib_cache_dir = Path(args.grib_cache_dir) if args.grib_cache_dir else DEFAULT_GRIB_CACHE_DIR
    try:
        rows = grib_inventory(args.date, args.cycle, args.forecast_hour, grib_cache_dir)
    except Exception as e:
        _output({"error": str(e)}, args.format)
        return 1
    _output(rows, args.format,
            ["shortName", "typeOfLevel", "level", "name"],
            ["Short", "Level Type", "Level", "Name"])
    return 0


KNOWN_COMMANDS = {"fetch", "list-cache", "list-remote", "inventory"}


def _add_format_arg(parser: argparse.ArgumentParser):
    """Add --format argument to a parser."""
    parser.add_argument(
        "--format", "-f",
        choices=["json", "table"],
        default="table",
        help="Output format (default: table)"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nbm_fetch.py",
        description="NBM weather data utility (fetch/list/inventory).",
    )
    sub = parser.add_subparsers(dest="command")

    p_fetch = sub.add_parser("fetch", help="Fetch daily min/max temps")
    p_fetch.add_argument("--lat", type=float, required=True)
    p_fetch.add_argument("--lon", type=float, required=True)
    p_fetch.add_argument("--date", type=str, required=True, help="Target date YYYY-MM-DD")
    p_fetch.add_argument("--as-of", type=str, default="", help="Point-in-time constraint (ISO-8601 UTC)")
    p_fetch.add_argument("--cache-dir", type=str, default="")
    p_fetch.add_argument("--no-cache", action="store_true", help="Bypass cache (re-download)")
    _add_format_arg(p_fetch)

    p_list = sub.add_parser("list-cache", help="List locally cached forecasts")
    p_list.add_argument("--date", type=str, default=None)
    p_list.add_argument("--lat", type=float, default=None)
    p_list.add_argument("--lon", type=float, default=None)
    p_list.add_argument("--cache-dir", type=str, default="")
    _add_format_arg(p_list)

    p_remote = sub.add_parser("list-remote", help="List NBM cycles on NOAA S3")
    g = p_remote.add_mutually_exclusive_group()
    g.add_argument("--date", type=str, default=None)
    g.add_argument("--days", type=int, default=None)
    _add_format_arg(p_remote)

    p_inv = sub.add_parser("inventory", help="List GRIB2 messages in one NBM file")
    p_inv.add_argument("--date", type=str, required=True)
    p_inv.add_argument("--cycle", type=int, required=True, choices=NBM_CYCLES)
    p_inv.add_argument("--forecast-hour", type=int, default=1)
    p_inv.add_argument("--grib-cache-dir", type=str, default="")
    _add_format_arg(p_inv)

    return parser


def main() -> int:
    argv = sys.argv[1:]
    # Backward-compat: bare `--lat ... --lon ... --date ...` means `fetch`.
    # Legacy callers expect JSON, so inject --format json.
    legacy_mode = not argv or (argv[0] not in KNOWN_COMMANDS and argv[0] not in ("-h", "--help"))
    if legacy_mode:
        argv = ["fetch", "--format", "json"] + argv

    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command == "fetch":
        return cmd_fetch(args)
    if args.command == "list-cache":
        return cmd_list_cache(args)
    if args.command == "list-remote":
        return cmd_list_remote(args)
    if args.command == "inventory":
        return cmd_inventory(args)

    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
