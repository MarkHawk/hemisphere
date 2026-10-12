#!/usr/bin/env python3
"""Live data for the Hemisphere "orbit watch" scene.

Fetches satellite orbits from CelesTrak and earthquakes from the USGS, works
out where every satellite is every few seconds, and writes small text files
that scenes/orbit.c reads:

    data/sats.txt     one line per satellite:  group lat lon alt_km name
    data/track.txt    the ISS path: past and next orbit,  minutes lat lon
    data/quakes.txt   one line per quake:  magnitude lat lon unix_time place

Orbit data is refreshed every 12 hours (CelesTrak asks for no more than
every 2), quakes every 5 minutes. If the network is down it carries on with
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
import time
import urllib.request
from datetime import datetime, timezone

from sgp4.api import Satrec, jday
from sgp4.propagation import gstime

CELESTRAK = "https://celestrak.org/NORAD/elements/gp.php?GROUP={}&FORMAT=tle"
# group file name -> one-letter code the scene uses for colour
GROUPS = {"stations": "S", "visual": "V", "gps-ops": "G", "weather": "W"}
USGS = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"
ISS = 25544
EARTH_R = 6371.0

TLE_EVERY = 12 * 3600
QUAKES_EVERY = 5 * 60
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
    ap.add_argument("--at", help="pretend it is this UTC time (YYYY-MM-DDTHH:MM), for testing")
    args = ap.parse_args()
    args.data.mkdir(parents=True, exist_ok=True)

    offset = 0.0
    if args.at:
        offset = datetime.fromisoformat(args.at).replace(tzinfo=timezone.utc).timestamp() - time.time()

    last_tle = last_quakes = last_track = 0.0
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
