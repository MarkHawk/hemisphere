# Live data for orbit watch and Weather

Part of [Hemisphere](../README.md). One small Python program, the orbit feed, fetches every piece of live data: satellites and earthquakes for Orbit watch and the Sci-fi radar, and clouds, aurora and storms for Weather.

Orbit watch needs `tools/orbitfeed.py` running. It downloads satellite orbits
from CelesTrak twice a day and earthquakes from the USGS every five
minutes, works out every satellite's position every five seconds with the
SGP4 orbit model, and writes small files to `data/` that the scene reads.
It also fetches the data for the Weather scene: a map of the world's clouds
every three hours (2048 x 1024, plus a sharper 4096 x 2048 one, cut down
to the area round home, which needs Pillow), NOAA's aurora forecast every
half hour, and, about once a month, the famous storms picked out of NOAA's
best-track archive. That last one is a big download, so it runs in the
background and the storms style says so until it is done. Start the feed
with `--no-weather` to skip all of it.
If the network drops, it carries on with what it last downloaded. Without
the feed, or if its data is more than a minute old, the scene still shows
the Earth and says "NO LIVE DATA" across the equator. The radar in the
sci-fi scene uses the same data. Orbits come from
[CelesTrak](https://celestrak.org) (space stations, bright satellites, GPS
and weather satellites) and earthquakes of magnitude 2.5 and over from the
[USGS](https://earthquake.usgs.gov) feed for the past day.

```bash
cd ~/hemisphere
python3 -m venv venv
venv/bin/pip install -r tools/requirements.txt
venv/bin/python3 tools/orbitfeed.py           # try it: Ctrl+C to stop
sudo cp tools/orbitfeed.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now orbitfeed         # start now and at every boot
journalctl -u orbitfeed -f                    # watch what it is doing
```

The service file assumes the program lives in `/home/pi/hemisphere` and
runs as `pi`; edit `User=` and the paths to match your Pi. `--once` updates everything once
and exits, which is handy for checking the network.
