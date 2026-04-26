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
import sqlite3
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

DEFAULT_DB_PATH = Path(os.getenv("NBM_DB_PATH", str(Path.home() / ".cache/predibloom/forecasts.db")))
DEFAULT_GRIB_CACHE_DIR = Path(os.getenv("NBM_GRIB_DIR", str(Path.home() / ".cache/predibloom/grib2")))
DEFAULT_NBM_BASE = Path(os.getenv("NBM_BASE_DIR", str(Path.home() / ".cache/predibloom/nbm")))
DEFAULT_GRID_INDEX_DB = DEFAULT_NBM_BASE / "index.db"

# Module-level pointer used by fetch_daily_temps() and friends; mutated by the
# fetch command handler when --db-path is supplied.
DB_PATH = DEFAULT_DB_PATH


# ---------------------------------------------------------------------------
# SQLite database helpers
# ---------------------------------------------------------------------------

def _ensure_grid_index_db(db_path: Path) -> sqlite3.Connection:
    """Open grid index database and ensure schema exists."""
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(db_path))
    conn.execute("""
        CREATE TABLE IF NOT EXISTS nbm_grids (
            id INTEGER PRIMARY KEY,
            cycle_date TEXT NOT NULL,
            cycle_hour INTEGER NOT NULL,
            forecast_hour INTEGER NOT NULL,
            variable TEXT NOT NULL,
            file_path TEXT NOT NULL,
            grid_shape TEXT,
            created_at TEXT DEFAULT (datetime('now')),
            UNIQUE (cycle_date, cycle_hour, forecast_hour, variable)
        )
    """)
    conn.commit()
    return conn


def _ensure_db(db_path: Path) -> sqlite3.Connection:
    """Open database and ensure schema exists."""
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(db_path))
    conn.execute("""
        CREATE TABLE IF NOT EXISTS nbm_forecasts (
            target_date   TEXT NOT NULL,
            cycle_hour    INTEGER NOT NULL,
            latitude      REAL NOT NULL,
            longitude     REAL NOT NULL,
            temp_max_f    REAL NOT NULL,
            temp_min_f    REAL NOT NULL,
            cycle_date    TEXT NOT NULL,
            hours_fetched INTEGER NOT NULL,
            created_at    TEXT DEFAULT (datetime('now')),
            PRIMARY KEY (target_date, cycle_hour, latitude, longitude)
        )
    """)
    conn.commit()
    return conn


def load_from_db(target_date: str, cycle_hour: int, lat: float, lon: float,
                 db_path: Path = None) -> Optional[dict]:
    """Load cached forecast from SQLite if available."""
    db = db_path or DB_PATH
    if not db.exists():
        return None
    try:
        conn = sqlite3.connect(str(db))
        conn.row_factory = sqlite3.Row
        cur = conn.execute("""
            SELECT target_date, cycle_hour, latitude, longitude,
                   temp_max_f, temp_min_f, cycle_date, hours_fetched
            FROM nbm_forecasts
            WHERE target_date = ?
              AND cycle_hour = ?
              AND ABS(latitude - ?) < 0.0005
              AND ABS(longitude - ?) < 0.0005
            LIMIT 1
        """, (target_date, cycle_hour, lat, lon))
        row = cur.fetchone()
        conn.close()
        if row:
            return {
                "date": row["target_date"],
                "temp_max_f": row["temp_max_f"],
                "temp_min_f": row["temp_min_f"],
                "cycle": f"{row['cycle_date']}T{row['cycle_hour']:02d}Z",
                "hours_fetched": row["hours_fetched"],
            }
    except sqlite3.Error:
        pass
    return None


def save_to_db(target_date: str, cycle_hour: int, lat: float, lon: float,
               data: dict, db_path: Path = None):
    """Save forecast to SQLite."""
    db = db_path or DB_PATH
    conn = _ensure_db(db)
    # Parse cycle_date from data["cycle"] which is like "2026-04-19T19Z"
    cycle_str = data.get("cycle", "")
    cycle_date = cycle_str.split("T")[0] if "T" in cycle_str else target_date
    conn.execute("""
        INSERT OR REPLACE INTO nbm_forecasts
            (target_date, cycle_hour, latitude, longitude,
             temp_max_f, temp_min_f, cycle_date, hours_fetched)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    """, (target_date, cycle_hour, lat, lon,
          data["temp_max_f"], data["temp_min_f"], cycle_date, data["hours_fetched"]))
    conn.commit()
    conn.close()


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


def check_cycle_available(cycle_date: str, cycle_hour: int) -> tuple[bool, str]:
    """Check if a cycle's files are available on S3.

    Returns (available, message) tuple.
    """
    s3fs_mod = _require_s3fs()
    fs = s3fs_mod.S3FileSystem(anon=True)

    # Check if f001 exists - if not, cycle isn't ready
    test_path = s3_grib_path(cycle_date, cycle_hour, 1).replace("s3://", "")
    try:
        if fs.exists(test_path):
            return True, "available"
        else:
            return False, f"Cycle {cycle_date} {cycle_hour:02d}Z not yet available on S3 (still uploading?)"
    except Exception as e:
        return False, f"S3 check failed: {e}"


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
                      use_cache: bool = True,
                      db_path: Path = None) -> dict:
    """Fetch daily min/max temperatures for a target date."""
    cycle_date, cycle_hour = find_best_cycle(target_date, as_of)

    if use_cache:
        cached = load_from_db(target_date, cycle_hour, lat, lon, db_path)
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

    save_to_db(target_date, cycle_hour, lat, lon, result, db_path)
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

def list_cache(db_path: Path, date: Optional[str] = None,
               lat: Optional[float] = None, lon: Optional[float] = None) -> list[dict]:
    """Query SQLite for cached forecasts."""
    if not db_path.exists():
        return []

    try:
        conn = sqlite3.connect(str(db_path))
        conn.row_factory = sqlite3.Row

        query = """
            SELECT target_date, cycle_hour, latitude, longitude,
                   temp_max_f, temp_min_f, hours_fetched
            FROM nbm_forecasts
            WHERE 1=1
        """
        params = []

        if date:
            query += " AND target_date = ?"
            params.append(date)
        if lat is not None:
            query += " AND ABS(latitude - ?) < 0.001"
            params.append(lat)
        if lon is not None:
            query += " AND ABS(longitude - ?) < 0.001"
            params.append(lon)

        query += " ORDER BY target_date, cycle_hour"

        cur = conn.execute(query, params)
        rows = []
        for row in cur:
            rows.append({
                "date": row["target_date"],
                "cycle": row["cycle_hour"],
                "lat": row["latitude"],
                "lon": row["longitude"],
                "temp_max_f": row["temp_max_f"],
                "temp_min_f": row["temp_min_f"],
                "hours_fetched": row["hours_fetched"],
            })
        conn.close()
        return rows
    except sqlite3.Error:
        return []


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
# Grid capture commands
# ---------------------------------------------------------------------------

# Maximum forecast hours in NBM (264 hours = 11 days)
NBM_MAX_FORECAST_HOUR = 264

# Default retention period in days
DEFAULT_RETENTION_DAYS = 30


def _grid_file_path(base_dir: Path, cycle_date: str, cycle_hour: int,
                    forecast_hour: int, variable: str = "2t") -> Path:
    """Return path for a NetCDF4 grid file."""
    date_str = cycle_date.replace("-", "")
    return (base_dir / "grids" / f"blend.{date_str}" /
            f"{cycle_hour:02d}" / f"{variable}.f{forecast_hour:03d}.nc")


def _download_and_extract_temp(cycle_date: str, cycle_hour: int,
                                forecast_hour: int, base_dir: Path,
                                index_db: Path) -> Optional[Path]:
    """Download GRIB2, extract 2m temp to NetCDF4, update index.

    Returns path to created NetCDF4 file, or None on failure.
    Returns "skip" (string) if file exists but has no 2t variable.
    """
    import numpy as np

    try:
        import pygrib
    except ImportError as e:
        print(f"Error: pygrib required: {e}", file=sys.stderr)
        return None

    try:
        from netCDF4 import Dataset
    except ImportError as e:
        print(f"Error: netCDF4 required: {e}", file=sys.stderr)
        return None

    nc_path = _grid_file_path(base_dir, cycle_date, cycle_hour, forecast_hour)

    # Skip if already exists
    if nc_path.exists():
        return nc_path

    s3_path = s3_grib_path(cycle_date, cycle_hour, forecast_hour)
    try:
        s3fs_mod = _require_s3fs()
        fs = s3fs_mod.S3FileSystem(anon=True)

        # Download GRIB2 to temp file
        with tempfile.NamedTemporaryFile(suffix=".grib2", delete=False) as tmp:
            tmp_path = tmp.name

        try:
            fs.get(s3_path, tmp_path)

            # Open GRIB2 and find 2m temperature
            gribs = pygrib.open(tmp_path)
            temp_msg = None
            for msg in gribs:
                if (msg.shortName == "2t" and
                    msg.typeOfLevel == "heightAboveGround" and
                    msg.level == 2):
                    temp_msg = msg
                    break

            if temp_msg is None:
                # Not a failure - this hour just doesn't have 2t (normal for hours > 36)
                gribs.close()
                return "skip"

            # Extract data
            data = temp_msg.values
            lats, lons = temp_msg.latlons()
            grid_shape = f"{data.shape[0]}x{data.shape[1]}"

            # Create output directory
            nc_path.parent.mkdir(parents=True, exist_ok=True)

            # Write NetCDF4 with compression
            with Dataset(str(nc_path), 'w', format='NETCDF4') as nc:
                # Create dimensions
                nc.createDimension('y', data.shape[0])
                nc.createDimension('x', data.shape[1])

                # Create variables with compression
                lat_var = nc.createVariable('latitude', 'f4', ('y', 'x'),
                                            zlib=True, complevel=4)
                lon_var = nc.createVariable('longitude', 'f4', ('y', 'x'),
                                            zlib=True, complevel=4)
                temp_var = nc.createVariable('temperature_2m', 'f4', ('y', 'x'),
                                             zlib=True, complevel=4,
                                             fill_value=np.nan)

                # Set attributes
                nc.title = f"NBM 2m Temperature"
                nc.source = s3_path
                nc.cycle_date = cycle_date
                nc.cycle_hour = cycle_hour
                nc.forecast_hour = forecast_hour

                lat_var.units = "degrees_north"
                lon_var.units = "degrees_east"
                temp_var.units = "K"
                temp_var.long_name = "2m Temperature"

                # Write data
                lat_var[:] = lats.astype(np.float32)
                lon_var[:] = lons.astype(np.float32)

                # Handle masked arrays
                if hasattr(data, 'mask'):
                    temp_data = data.data.astype(np.float32)
                    temp_data[data.mask] = np.nan
                    temp_var[:] = temp_data
                else:
                    temp_var[:] = data.astype(np.float32)

            gribs.close()

            # Update index database
            conn = _ensure_grid_index_db(index_db)
            conn.execute("""
                INSERT OR REPLACE INTO nbm_grids
                    (cycle_date, cycle_hour, forecast_hour, variable, file_path, grid_shape)
                VALUES (?, ?, ?, ?, ?, ?)
            """, (cycle_date, cycle_hour, forecast_hour, "2t",
                  str(nc_path.relative_to(base_dir)), grid_shape))
            conn.commit()
            conn.close()

            # Keep the GRIB2 file for future use
            grib_path = nc_path.parent / f"core.f{forecast_hour:03d}.grib2"
            if not grib_path.exists():
                import shutil
                shutil.move(tmp_path, grib_path)
                tmp_path = None  # Mark as moved

            return nc_path

        finally:
            if tmp_path and os.path.exists(tmp_path):
                os.unlink(tmp_path)

    except Exception as e:
        print(f"Error capturing f{forecast_hour:03d}: {e}", file=sys.stderr)
        return None


def capture_cycle(cycle_date: str, cycle_hour: int, base_dir: Path,
                  index_db: Path, forecast_hours: Optional[list[int]] = None,
                  verbose: bool = True, cycle_progress: str = "") -> dict:
    """Capture a full NBM cycle, extracting 2m temp to NetCDF4.

    Returns dict with counts: {"success": N, "failed": N, "skipped": N, "error": str|None}

    Args:
        cycle_progress: Optional string like "[3/40]" for overall progress display
    """
    if forecast_hours is None:
        # Default to hours 1-36 (all hours with 2m temperature)
        forecast_hours = list(range(1, 37))

    # Pre-check: is the cycle available on S3?
    available, msg = check_cycle_available(cycle_date, cycle_hour)
    if not available:
        if verbose:
            print(f"\n{msg}", file=sys.stderr)
        return {"success": 0, "failed": 0, "skipped": 0, "error": msg}

    result = {"success": 0, "failed": 0, "skipped": 0}
    total = len(forecast_hours)
    prefix = f"{cycle_progress} " if cycle_progress else ""

    for i, fhr in enumerate(forecast_hours, 1):
        nc_path = _grid_file_path(base_dir, cycle_date, cycle_hour, fhr)
        if nc_path.exists():
            result["skipped"] += 1
            status = "skip"
        else:
            path = _download_and_extract_temp(cycle_date, cycle_hour, fhr,
                                               base_dir, index_db)
            if path == "skip":
                # Hour doesn't have 2t variable (normal for hours > 36)
                result["skipped"] += 1
                status = "no2t"
            elif path:
                result["success"] += 1
                status = "ok"
            else:
                result["failed"] += 1
                status = "FAIL"

        if verbose:
            pct = (i * 100) // total
            # Progress bar with percentage
            print(f"\r{prefix}{cycle_date} {cycle_hour:02d}Z  f{fhr:03d} [{pct:3d}%] {status:4s} | "
                  f"+{result['success']} skip:{result['skipped']} fail:{result['failed']}   ",
                  end="", file=sys.stderr, flush=True)

    if verbose:
        print(file=sys.stderr)  # newline after progress

    return result


def list_available_cycles(days: int = 10) -> list[dict]:
    """List NBM cycles available on S3."""
    return list_remote(days=days)


def list_captured_cycles(index_db: Path) -> list[dict]:
    """List cycles in the local grid index."""
    if not index_db.exists():
        return []

    conn = sqlite3.connect(str(index_db))
    conn.row_factory = sqlite3.Row
    cur = conn.execute("""
        SELECT cycle_date, cycle_hour, COUNT(*) as file_count,
               MIN(forecast_hour) as fhr_min, MAX(forecast_hour) as fhr_max
        FROM nbm_grids
        GROUP BY cycle_date, cycle_hour
        ORDER BY cycle_date DESC, cycle_hour DESC
    """)
    rows = []
    for r in cur:
        rows.append({
            "cycle_date": r["cycle_date"],
            "cycle_hour": r["cycle_hour"],
            "file_count": r["file_count"],
            "fhr_min": r["fhr_min"],
            "fhr_max": r["fhr_max"],
        })
    conn.close()
    return rows


def cleanup_old_grids(base_dir: Path, index_db: Path,
                      older_than_days: int) -> dict:
    """Delete grid files older than specified days.

    Returns dict with counts: {"deleted_files": N, "deleted_cycles": N}
    """
    from datetime import datetime, timedelta

    cutoff = datetime.now() - timedelta(days=older_than_days)
    cutoff_date = cutoff.strftime("%Y-%m-%d")

    result = {"deleted_files": 0, "deleted_cycles": 0}

    if not index_db.exists():
        return result

    conn = sqlite3.connect(str(index_db))
    conn.row_factory = sqlite3.Row

    # Find old entries
    cur = conn.execute("""
        SELECT id, cycle_date, cycle_hour, file_path
        FROM nbm_grids
        WHERE cycle_date < ?
    """, (cutoff_date,))

    ids_to_delete = []
    cycles_seen = set()

    for r in cur:
        ids_to_delete.append(r["id"])
        cycles_seen.add((r["cycle_date"], r["cycle_hour"]))

        # Delete file
        file_path = base_dir / r["file_path"]
        if file_path.exists():
            try:
                file_path.unlink()
                result["deleted_files"] += 1
            except OSError:
                pass

    # Delete index entries
    if ids_to_delete:
        placeholders = ",".join("?" * len(ids_to_delete))
        conn.execute(f"DELETE FROM nbm_grids WHERE id IN ({placeholders})",
                     ids_to_delete)
        conn.commit()

    conn.close()

    result["deleted_cycles"] = len(cycles_seen)

    # Clean up empty directories
    grids_dir = base_dir / "grids"
    if grids_dir.exists():
        for blend_dir in grids_dir.iterdir():
            if blend_dir.is_dir():
                # Check if any files remain
                has_files = any(blend_dir.rglob("*.nc"))
                if not has_files:
                    try:
                        shutil.rmtree(str(blend_dir))
                    except OSError:
                        pass

    return result


# ---------------------------------------------------------------------------
# Command dispatch
# ---------------------------------------------------------------------------

def cmd_fetch(args: argparse.Namespace) -> int:
    global DB_PATH
    if args.db_path:
        DB_PATH = Path(args.db_path)

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

    db_path = Path(args.db_path) if args.db_path else DB_PATH
    result = fetch_daily_temps(args.date, args.lat, args.lon, as_of,
                               use_cache=not args.no_cache,
                               db_path=db_path)
    _output(result, args.format)
    return 1 if "error" in result else 0


def cmd_list_cache(args: argparse.Namespace) -> int:
    db_path = Path(args.db_path) if args.db_path else DB_PATH
    rows = list_cache(db_path, date=args.date, lat=args.lat, lon=args.lon)
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


def cmd_capture(args: argparse.Namespace) -> int:
    """Capture one or more NBM cycles to local NetCDF4 storage."""
    base_dir = Path(args.base_dir) if args.base_dir else DEFAULT_NBM_BASE
    index_db = base_dir / "index.db"

    # Parse forecast hours if specified (default: 1-36, the hours with 2t)
    if args.forecast_hours:
        if "-" in args.forecast_hours:
            start, end = args.forecast_hours.split("-")
            forecast_hours = list(range(int(start), int(end) + 1))
        else:
            forecast_hours = [int(x) for x in args.forecast_hours.split(",")]
    else:
        # Default to hours 1-36 which all have 2m temperature
        forecast_hours = list(range(1, 37))

    cycles_to_capture = []

    if args.cycle is not None:
        # Single cycle specified
        cycles_to_capture.append((args.date, args.cycle))
    else:
        # All cycles for the date
        for ch in NBM_CYCLES:
            cycles_to_capture.append((args.date, ch))

    total = {"success": 0, "failed": 0, "skipped": 0}

    for cycle_date, cycle_hour in cycles_to_capture:
        print(f"Capturing {cycle_date} {cycle_hour:02d}Z...", file=sys.stderr)
        result = capture_cycle(cycle_date, cycle_hour, base_dir, index_db,
                               forecast_hours, verbose=True)
        for k in total:
            total[k] += result[k]

    _output(total, args.format,
            ["success", "failed", "skipped"],
            ["Success", "Failed", "Skipped"])
    return 0 if total["failed"] == 0 else 1


def cmd_capture_missing(args: argparse.Namespace) -> int:
    """Scan S3 for available cycles, download missing ones."""
    base_dir = Path(args.base_dir) if args.base_dir else DEFAULT_NBM_BASE
    index_db = base_dir / "index.db"

    # Get available cycles from S3
    print(f"Scanning S3 for last {args.days} days...", file=sys.stderr)
    try:
        available = list_available_cycles(days=args.days)
    except Exception as e:
        _output({"error": f"S3 scan failed: {e}"}, args.format)
        return 1

    # Get captured cycles (consider captured if we have at least 10 forecast hours)
    captured = list_captured_cycles(index_db)
    captured_set = {(c["cycle_date"], c["cycle_hour"]) for c in captured
                    if c["file_count"] >= 10}

    # Find missing
    missing = []
    for a in available:
        key = (a["date"], a["cycle"])
        if key not in captured_set:
            missing.append(key)

    if not missing:
        print("All available cycles are captured.", file=sys.stderr)
        _output({"missing": 0, "captured": 0}, args.format)
        return 0

    print(f"Found {len(missing)} missing cycles\n", file=sys.stderr)

    import time
    total = {"success": 0, "failed": 0, "skipped": 0}
    num_cycles = len(missing)
    start_time = time.time()

    for idx, (cycle_date, cycle_hour) in enumerate(missing, 1):
        cycle_progress = f"[{idx}/{num_cycles}]"
        result = capture_cycle(cycle_date, cycle_hour, base_dir, index_db,
                               verbose=True, cycle_progress=cycle_progress)
        for k in total:
            total[k] += result[k]

        # Show elapsed and estimated time
        elapsed = time.time() - start_time
        if idx > 0:
            per_cycle = elapsed / idx
            remaining = per_cycle * (num_cycles - idx)
            elapsed_str = f"{int(elapsed // 60)}m{int(elapsed % 60):02d}s"
            remaining_str = f"{int(remaining // 60)}m{int(remaining % 60):02d}s"
            print(f"  elapsed: {elapsed_str}  remaining: ~{remaining_str}", file=sys.stderr)

    _output(total, args.format,
            ["success", "failed", "skipped"],
            ["Success", "Failed", "Skipped"])
    return 0 if total["failed"] == 0 else 1


def cmd_cleanup(args: argparse.Namespace) -> int:
    """Delete grid files older than specified retention period."""
    base_dir = Path(args.base_dir) if args.base_dir else DEFAULT_NBM_BASE
    index_db = base_dir / "index.db"

    print(f"Cleaning up files older than {args.older_than} days...", file=sys.stderr)
    result = cleanup_old_grids(base_dir, index_db, args.older_than)

    _output(result, args.format,
            ["deleted_files", "deleted_cycles"],
            ["Files Deleted", "Cycles Deleted"])
    return 0


def cmd_grids(args: argparse.Namespace) -> int:
    """List captured grid cycles."""
    base_dir = Path(args.base_dir) if args.base_dir else DEFAULT_NBM_BASE
    index_db = base_dir / "index.db"

    rows = list_captured_cycles(index_db)

    _output(rows, args.format,
            ["cycle_date", "cycle_hour", "file_count", "fhr_min", "fhr_max"],
            ["Date", "Cycle", "Files", "FHR Min", "FHR Max"])
    return 0


KNOWN_COMMANDS = {"fetch", "list-cache", "list-remote", "inventory",
                  "capture", "capture-missing", "cleanup", "grids"}


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
    p_fetch.add_argument("--db-path", type=str, default="")
    p_fetch.add_argument("--no-cache", action="store_true", help="Bypass cache (re-download)")
    _add_format_arg(p_fetch)

    p_list = sub.add_parser("list-cache", help="List locally cached forecasts")
    p_list.add_argument("--date", type=str, default=None)
    p_list.add_argument("--lat", type=float, default=None)
    p_list.add_argument("--lon", type=float, default=None)
    p_list.add_argument("--db-path", type=str, default="")
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

    # Grid capture commands
    p_capture = sub.add_parser("capture", help="Capture NBM cycle(s) to local NetCDF4")
    p_capture.add_argument("--date", type=str, required=True, help="Cycle date YYYY-MM-DD")
    p_capture.add_argument("--cycle", type=int, choices=NBM_CYCLES,
                           help="Cycle hour (1, 7, 13, or 19). If omitted, captures all cycles.")
    p_capture.add_argument("--forecast-hours", type=str,
                           help="Forecast hours: '1-264' or '1,2,3'. Default: 1-36 (all hours with 2t)")
    p_capture.add_argument("--base-dir", type=str, default="")
    _add_format_arg(p_capture)

    p_capture_missing = sub.add_parser("capture-missing",
                                        help="Download missing cycles from S3")
    p_capture_missing.add_argument("--days", type=int, default=10,
                                   help="How many days back to scan S3 (default 10)")
    p_capture_missing.add_argument("--base-dir", type=str, default="")
    _add_format_arg(p_capture_missing)

    p_cleanup = sub.add_parser("cleanup", help="Delete old grid files")
    p_cleanup.add_argument("--older-than", type=int, default=DEFAULT_RETENTION_DAYS,
                           help=f"Delete files older than N days (default {DEFAULT_RETENTION_DAYS})")
    p_cleanup.add_argument("--base-dir", type=str, default="")
    _add_format_arg(p_cleanup)

    p_grids = sub.add_parser("grids", help="List captured grid cycles")
    p_grids.add_argument("--base-dir", type=str, default="")
    _add_format_arg(p_grids)

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
    if args.command == "capture":
        return cmd_capture(args)
    if args.command == "capture-missing":
        return cmd_capture_missing(args)
    if args.command == "cleanup":
        return cmd_cleanup(args)
    if args.command == "grids":
        return cmd_grids(args)

    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
