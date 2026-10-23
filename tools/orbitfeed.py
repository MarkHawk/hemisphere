#!/usr/bin/env python3
"""Live data for the Hemisphere "orbit watch" scene.

Fetches satellite orbits from CelesTrak and earthquakes from the USGS, works
out where every satellite is every few seconds, and writes small text files
that scenes/orbit.c reads:

    data/sats.txt     one line per satellite:  group lat lon alt_km name
    data/track.txt    the ISS path: past and next orbit,  minutes lat lon
    data/quakes.txt   one line per quake:  magnitude lat lon unix_time place
For the weather scene (scenes/weather.c):

    data/clouds.jpg        the world's clouds, 2048 x 1024, from Matt Eason's
                           live cloud maps (clouds.matteason.co.uk, made from
                           EUMETSAT satellite data, new every 3 hours)
    data/clouds_local.jpg  a sharper cut-out round home (from the 4096 x 2048
    data/clouds_local.txt  map; needs Pillow), and its edges: lon0 lon1 lat0
                           lat1 in degrees. Home comes from weather.cfg
    data/aurora.pgm        NOAA's 30-minute aurora forecast, 360 x 181, every
                           30 minutes
    data/storms.txt        famous tropical storms from NOAA's IBTrACS best-track
                           archive, fetched once (a big download, about 30 days)

Orbit data is refreshed every 12 hours (CelesTrak asks for no more than
every 2), quakes every 5 minutes, clouds every 3 hours, the aurora every 30 minutes
and the storms once a month (use --no-weather to skip all the weather data). If the network is down it carries on with
what it last downloaded.

    python3 -m venv venv
    venv/bin/pip install -r tools/requirements.txt
    venv/bin/python3 tools/orbitfeed.py

Run it as a service with tools/orbitfeed.service.
"""
import argparse
import json
import math
import os
import pathlib
import sys
import threading
import time
import urllib.request
from datetime import datetime, timezone

from sgp4.api import Satrec, jday
from sgp4.propagation import gstime

CELESTRAK = "https://celestrak.org/NORAD/elements/gp.php?GROUP={}&FORMAT=tle"
# group file name -> one-letter code the scene uses for colour
GROUPS = {"stations": "S", "visual": "V", "gps-ops": "G", "weather": "W"}
USGS = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"
CLOUDS = "https://clouds.matteason.co.uk/images/2048x1024/clouds.jpg"
CLOUDS_BIG = "https://clouds.matteason.co.uk/images/4096x2048/clouds.jpg"
AURORA = "https://services.swpc.noaa.gov/json/ovation_aurora_latest.json"
IBTRACS = ("https://www.ncei.noaa.gov/data/international-best-track-archive-for-climate-stewardship-ibtracs/"
           "v04r01/access/csv/ibtracs.since1980.list.v04r01.csv")
# (name, season): replayed in this order
STORMS = [("GILBERT", 1988), ("ANDREW", 1992), ("MITCH", 1998), ("KATRINA", 2005), ("WILMA", 2005),
          ("NARGIS", 2008), ("SANDY", 2012), ("HAIYAN", 2013), ("PATRICIA", 2015), ("IRMA", 2017),
          ("MARIA", 2017), ("OPHELIA", 2017), ("DORIAN", 2019), ("LORENZO", 2019), ("HAGIBIS", 2019),
          ("IDAI", 2019), ("IAN", 2022), ("FREDDY", 2023), ("MILTON", 2024)]
ISS = 25544
EARTH_R = 6371.0

TLE_EVERY = 12 * 3600
QUAKES_EVERY = 5 * 60
CLOUDS_EVERY = 3 * 3600
AURORA_EVERY = 30 * 60
STORMS_EVERY = 30 * 86400
POSITIONS_EVERY = 5
TRACK_EVERY = 60


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def fetch(url, timeout=20):
    req = urllib.request.Request(url, headers={"User-Agent": "hemisphere-orbitfeed/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def write_atomic(path, text):
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text)
    # On Windows the swap fails while hemisphere has the file open for its
    # brief read, so try again a moment later
    for attempt in range(20):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05)


def fetch_bytes(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": "hemisphere-orbitfeed/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def replace_file(tmp, path):
    # On Windows the swap fails while hemisphere has the file open, so retry
    for attempt in range(20):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05)


def stale(path, every):
    return not path.exists() or time.time() - path.stat().st_mtime >= every


def retry_later(path, every, minutes=15):
    """After a failed download, try again in a few minutes, not at once."""
    if path.exists():
        t = time.time() - every + minutes * 60
        os.utime(path, (t, t))


def home(data):
    """Home from the weather scene's settings, else Greenwich."""
    lat, lon = 51.48, 0.0
    cfg = data.parent / "weather.cfg"
    try:
        for line in cfg.read_text().splitlines():
            bits = line.split()
            if len(bits) >= 2 and bits[0] == "home_lat":
                lat = float(bits[1])
            if len(bits) >= 2 and bits[0] == "home_lon":
                lon = float(bits[1])
    except (OSError, ValueError):
        pass
    return lat, lon


def refresh_clouds(data):
    """The world's clouds, and a sharper cut-out round home."""
    path = data / "clouds.jpg"
    if not stale(path, CLOUDS_EVERY):
        return
    try:
        img = fetch_bytes(CLOUDS)
        if len(img) < 20000 or img[:2] != b"\xff\xd8":
            raise ValueError("not a JPEG")
        tmp = path.with_suffix(".jpg.tmp")
        tmp.write_bytes(img)
        replace_file(tmp, path)
        log(f"clouds: updated ({len(img) // 1024} KB)")
    except Exception as e:
        retry_later(path, CLOUDS_EVERY)
        log(f"clouds: not updated ({e})")
        return
    try:
        from PIL import Image
    except ImportError:
        log("clouds: no Pillow, so no sharp picture round home (pip install pillow)")
        return
    try:
        import io
        big = Image.open(io.BytesIO(fetch_bytes(CLOUDS_BIG, timeout=120))).convert("L")
        w, h = big.size
        lat, lon = home(data)
        lat0, lat1 = max(-90.0, lat - 32), min(90.0, lat + 32)
        lon0, lon1 = lon - 50, lon + 50
        # cut it out, wrapping round the date line if needed
        x0 = int((lon0 + 180) / 360 * w)
        x1 = int((lon1 + 180) / 360 * w)
        y0 = int((90 - lat1) / 180 * h)
        y1 = int((90 - lat0) / 180 * h)
        part = Image.new("L", (x1 - x0, y1 - y0))
        for off in (0, w, -w):
            a, b = max(x0 + off, 0), min(x1 + off, w)
            if a < b:
                part.paste(big.crop((a, y0, b, y1)), (a - (x0 + off), 0))
        tmp = data / "clouds_local.jpg.tmp"
        part.save(tmp, "JPEG", quality=88)
        replace_file(tmp, data / "clouds_local.jpg")
        write_atomic(data / "clouds_local.txt", f"{lon0:.3f} {lon1:.3f} {lat0:.3f} {lat1:.3f}\n")
        log(f"clouds: sharp picture round home ({part.size[0]}x{part.size[1]})")
    except Exception as e:
        log(f"clouds: no sharp picture round home ({e})")


def refresh_aurora(data):
    """NOAA's aurora forecast as a 360 x 181 grey picture (lon 0..359 across,
    lat -90 at the top row, +90 at the bottom), 0..100 % scaled to 0..255."""
    path = data / "aurora.pgm"
    if not stale(path, AURORA_EVERY):
        return
    try:
        feed = json.loads(fetch_bytes(AURORA).decode("utf-8"))
        grid = bytearray(360 * 181)
        for lon, lat, val in feed["coordinates"]:
            x, y = int(round(lon)) % 360, int(round(lat)) + 90
            if 0 <= y < 181:
                grid[y * 360 + x] = max(0, min(255, int(val * 2.55)))
        tmp = path.with_suffix(".pgm.tmp")
        tmp.write_bytes(b"P5 360 181 255\n" + bytes(grid))
        replace_file(tmp, path)
        log(f"aurora: forecast for {feed.get('Forecast Time', '?')}")
    except Exception as e:
        retry_later(path, AURORA_EVERY, 10)
        log(f"aurora: not updated ({e})")


def refresh_storms(data):
    """Pick the famous storms out of IBTrACS (read as it downloads; the file
    is big, so this happens rarely). Writes, per storm, an S line (name
    season) then P lines: unix_time lat lon wind_kt category."""
    path = data / "storms.txt"
    if not stale(path, STORMS_EVERY):
        return
    import csv
    import io
    want = {(n, y) for n, y in STORMS}
    found = {}                       # (name, season) -> {sid: [points]}
    try:
        log("storms: downloading the best-track archive (this takes a while)")
        req = urllib.request.Request(IBTRACS, headers={"User-Agent": "hemisphere-orbitfeed/1.0"})
        with urllib.request.urlopen(req, timeout=120) as r:
            rows = csv.reader(io.TextIOWrapper(r, encoding="utf-8", errors="replace"))
            head = next(rows)
            col = {name.strip(): i for i, name in enumerate(head)}
            for row in rows:
                try:
                    season = int(row[col["SEASON"]])
                except (ValueError, IndexError):
                    continue          # the units row
                name = row[col["NAME"]].strip().upper()
                if (name, season) not in want:
                    continue
                try:
                    lat = float(row[col["LAT"]]); lon = float(row[col["LON"]])
                except ValueError:
                    continue
                wind = 0.0
                for k in ("USA_WIND", "WMO_WIND"):
                    try:
                        wind = float(row[col[k]]); break
                    except (ValueError, KeyError):
                        pass
                try:
                    cat = int(row[col["USA_SSHS"]])
                except (ValueError, KeyError):
                    cat = -1 if wind < 34 else 0 if wind < 64 else 1 if wind < 83 else 2 if wind < 96 else 3 if wind < 113 else 4 if wind < 137 else 5
                cat = max(-1, cat)
                t = datetime.strptime(row[col["ISO_TIME"]].strip(), "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
                found.setdefault((name, season), {}).setdefault(row[col["SID"]], []).append(
                    (int(t.timestamp()), lat, lon, wind, cat))
    except Exception as e:
        retry_later(path, STORMS_EVERY, 60 * 6)
        log(f"storms: not updated ({e})")
        return
    out = []
    for key in STORMS:
        if key not in found:
            continue
        pts = max(found[key].values(), key=lambda p: max(q[3] for q in p))   # the strongest of that name
        pts.sort()
        out.append(f"S {key[0]} {key[1]}")
        out += [f"P {t} {la:.2f} {lo:.2f} {w:.0f} {c}" for t, la, lo, w, c in pts]
    if out:
        write_atomic(path, "\n".join(out) + "\n")
    log(f"storms: {sum(1 for l in out if l.startswith('S'))} of {len(STORMS)} found")


def refresh_tle(data, force=False):
    """Download each group's orbits if the cached copy is old or missing."""
    for group in GROUPS:
        path = data / f"tle_{group}.txt"
        if not force and path.exists() and time.time() - path.stat().st_mtime < TLE_EVERY:
            continue
        try:
            text = fetch(CELESTRAK.format(group))
            if "1 " not in text:
                raise ValueError("no orbit data in reply")
            write_atomic(path, text)
            log(f"orbits: {group} updated")
        except Exception as e:  # keep the old copy
            log(f"orbits: {group} not updated ({e})")


def load_sats(data):
    """Read the cached orbit files: [(group code, name, Satrec)], ISS first."""
    sats, seen = [], set()
    for group, code in GROUPS.items():
        path = data / f"tle_{group}.txt"
        if not path.exists():
            continue
        lines = [l.rstrip() for l in path.read_text().splitlines() if l.strip()]
        for i in range(len(lines) - 2):
            name, l1, l2 = lines[i], lines[i + 1], lines[i + 2]
            if not (l1.startswith("1 ") and l2.startswith("2 ")) or name.startswith(("1 ", "2 ")):
                continue
            try:
                sat = Satrec.twoline2rv(l1, l2)
            except Exception:
                continue
            if sat.satnum in seen:
                continue
            seen.add(sat.satnum)
            c = "I" if sat.satnum == ISS else code
            sats.append((c, name.strip().replace(" ", "_")[:24], sat))
    sats.sort(key=lambda s: s[0] != "I")
    return sats


def subpoint(sat, unix):
    """Latitude, longitude (degrees) and altitude (km) at a unix time."""
    dt = datetime.fromtimestamp(unix, timezone.utc)
    jd, fr = jday(dt.year, dt.month, dt.day, dt.hour, dt.minute,
                  dt.second + dt.microsecond / 1e6)
    err, r, _ = sat.sgp4(jd, fr)
    if err:
        return None
    x, y, z = r
    # TEME to Earth-fixed: turn by Greenwich sidereal time (good to a few km)
    g = gstime(jd + fr)
    lon = math.degrees(math.atan2(y, x) - g)
    lon = (lon + 180.0) % 360.0 - 180.0
    lat = math.degrees(math.atan2(z, math.hypot(x, y)))
    alt = math.sqrt(x * x + y * y + z * z) - EARTH_R
    return lat, lon, alt


def write_positions(data, sats, unix):
    out = [f"t {int(unix)}"]
    for code, name, sat in sats:
        p = subpoint(sat, unix)
        if p:
            out.append(f"{code} {p[0]:.3f} {p[1]:.3f} {p[2]:.1f} {name}")
    write_atomic(data / "sats.txt", "\n".join(out) + "\n")
    return len(out) - 1


def write_track(data, sats, unix):
    iss = next((s for c, _, s in sats if c == "I"), None)
    if iss is None:
        return
    out = []
    for m in range(-15, 96):            # 15 minutes back, one orbit ahead
        p = subpoint(iss, unix + m * 60)
        if p:
            out.append(f"{m} {p[0]:.3f} {p[1]:.3f}")
    write_atomic(data / "track.txt", "\n".join(out) + "\n")


def refresh_quakes(data):
    try:
        feed = json.loads(fetch(USGS))
    except Exception as e:
        log(f"quakes: not updated ({e})")
        return
    out = []
    for f in feed.get("features", []):
        p, g = f.get("properties", {}), f.get("geometry", {})
        if not g or p.get("mag") is None:
            continue
        lon, lat = g["coordinates"][0], g["coordinates"][1]
        place = (p.get("place") or "").replace("\n", " ")[:60]
        out.append(f"{p['mag']:.1f} {lat:.3f} {lon:.3f} {int(p['time'] / 1000)} {place}")
    write_atomic(data / "quakes.txt", "\n".join(out) + "\n")
    log(f"quakes: {len(out)} in the last day or so")


def main():
    here = pathlib.Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description="live data for the orbit watch scene")
    ap.add_argument("--data", type=pathlib.Path, default=here.parent / "data")
    ap.add_argument("--once", action="store_true", help="update everything once and exit")
    ap.add_argument("--no-weather", action="store_true", help="don't download clouds, aurora or storms")
    ap.add_argument("--at", help="pretend it is this UTC time (YYYY-MM-DDTHH:MM), for testing")
    args = ap.parse_args()
    args.data.mkdir(parents=True, exist_ok=True)

    offset = 0.0
    if args.at:
        offset = datetime.fromisoformat(args.at).replace(tzinfo=timezone.utc).timestamp() - time.time()

    last_tle = last_quakes = last_track = last_clouds = 0.0
    storms_busy = threading.Event()
    sats = []
    while True:
        now = time.time()
        # check hourly (every 5 minutes while there is no data at all);
        # refresh_tle only downloads when the cached copy is 12 hours old
        if now - last_tle > (3600 if sats else 300):
            refresh_tle(args.data)
            sats = load_sats(args.data)
            log(f"orbits: tracking {len(sats)} satellites")
            last_tle = now
        if now - last_quakes > QUAKES_EVERY:
            refresh_quakes(args.data)
            last_quakes = now
        if not args.no_weather and now - last_clouds > 300:
            refresh_clouds(args.data)
            refresh_aurora(args.data)
            if not storms_busy.is_set():          # a long download: in the background
                storms_busy.set()
                threading.Thread(target=lambda: (refresh_storms(args.data), storms_busy.clear()), daemon=True).start()
            last_clouds = now
        if sats:
            write_positions(args.data, sats, now + offset)
            if now - last_track > TRACK_EVERY:
                write_track(args.data, sats, now + offset)
                last_track = now
        if args.once:
            return 0
        time.sleep(POSITIONS_EVERY)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
