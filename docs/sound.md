# Sound and Now playing

Part of [Hemisphere](../README.md). Two feeds from your home network: sound from WLED lights, and what a HEOS amp is playing. Both are optional.

## Sound from WLED

Hemisphere can react to music, using the sound analysis your WLED lights
already share. In WLED's AudioReactive settings, set one device (the one
with the microphone) to **Send** under Audio Sync, V2 format. Hemisphere
picks the packets up by itself: they go to the multicast group `239.0.0.1`
on port 11988, so no IP addresses are needed. It prints
`Audio: listening on 239.0.0.1:11988` at start-up.

Scenes only react if their `audio` setting is 1. It is 0 (off) for every
scene that isn't built for music.

| Variable | Effect |
|----------|--------|
| `DOME_AUDIO_PORT=n` | Listen on another port (WLED's default is 11988); 0 turns sound off |
| `DOME_AUDIO_GROUP=a.b.c.d` | Listen to another multicast group (WLED's is 239.0.0.1) |
| `DOME_AUDIO_DEBUG=1` | Print the levels, beat, tempo and packet rate twice a second |

The web remote's `/api/state` shows the same values under `audio`.

**What it hears.** Of WLED's 16 bands, Hemisphere makes four smooth levels,
each scaled to the loudest it has heard lately: **bass** (bands 0-2),
**body** (3-6, the weight of the snare), **mid** (7-12, usually quiet) and
**air** (13-15, the snare and hi-hat). In most music the bass never stops,
so a loud bass is not a beat; the **beat** is a sharp jump in the air
bands. From the beats it works out the **tempo**. If the packets stop for a
second, everything eases back to nothing.

**Testing with no music.** `tools/audio/billie.txt` is one minute of real
audio sync data. Replay it as live packets:

```
python3 tools/audio/replay.py                  # this machine only
DOME_AUDIO_DEBUG=1 ./hemisphere                # in another terminal
```

The tempo should settle at about 117 BPM. By default the replay stays on
the machine it runs on; `--ttl 1` sends it across your network (to test the
Pi from a PC), but then your WLED lights will dance to it too. If a WLED
device is already sending, Hemisphere keeps following that one, so give
the replay its own port: `--port 11989` with `DOME_AUDIO_PORT=11989`.

To make your own scene react, see [Making a scene react](writing-scenes.md#making-a-scene-react).

`dome/audio.c` and `audio.h` need nothing from the rest of Hemisphere, so
they can be lifted into another project.

**Credit.** The sound comes from [WLED](https://kno.wled.ge)'s AudioReactive
Audio Sync (V2). No WLED code is included: the packet format follows WLED's
public Sound Sync docs and was checked against live packets.

## Now playing from a HEOS amp

If your amplifier or streamer speaks HEOS (Denon and Marantz network
models), `tools/nowplaying/nowplaying.py` follows what it is playing and
hands it to the dome:

* **the cover**, downloaded once, cached in `data/nowplaying/cache/` and
  resized to 512 x 512 (`data/nowplaying/art.rgba`, read straight into a
  texture). The Pi 3's GPU only mipmaps power-of-two sizes, covers come at
  300 to 640 pixels, and the front of the globe is about 600 pixels across,
  so 512 is the useful size. Covers stay on the Pi and are never committed.
* **the palette**: four colours from the cover. Music's theme 8, album,
  is built from them, so any Music style takes on the album's colours.
  With no cover it falls back to a neutral set.
* **the details** (title, artist, album or station, source, and what the
  HEOS queue plays next) in `data/nowplaying/now.txt`, the **progress**
  about once a second, and the **volume**, mute, repeat and shuffle as
  they change.
* **switching**: when music starts it shows the Now playing scene in a
  style that suits the source (the record planet for phono and CD, the
  album globe for the rest; Music, in the album colours, if there is no
  Now playing scene), and when it has been paused or stopped for five
  minutes it puts back the scene and style that were there. It never switches within ten minutes of
  you changing scene by hand (keyboard or remote), never over a calibration
  grid, and if you change scene while it has the dome it leaves the dome
  alone until the music has stopped for a while. The playlist waits while
  the music has the dome and carries on afterwards. `--no-auto` turns
  switching off.

It is **read only**: it only asks the amp what it is playing (and its
volume, mute, play mode and queue) and listens for changes. It cannot play, pause, skip, change the volume or the input:
anything else is refused before it is sent (`ALLOWED` in `heos.py`).

What each source does is in `tools/nowplaying/sources.json`: streaming
services show the cover, radio shows the station's picture if there is
one, phono is a record (no cover needed), the CD player on the coaxial
input (or the analogue CD input) is a disc (keeps the current colours),
other inputs show what
there is, and so does anything unknown. A source set to `release` (none
by default; a TV input, say) gives the dome back after ten seconds.
Inputs are shown by a short label ("Phono", "CD") rather than the amp's
own "<player> - Coaxial In". For inputs where the amp cannot tell
whether music is playing (a
turntable, a CD player), sound from your WLED audio sync counts as
playing. Copy the file, change it and pass it with `--config` to suit
your own inputs. The times are in there too.

```bash
cd ~/hemisphere
venv/bin/pip install -r tools/nowplaying/requirements.txt
HEOS_HOST=<your amp's address> venv/bin/python3 tools/nowplaying/nowplaying.py
```

Leave out `HEOS_HOST` and it searches the network. `HEOS_PLAYER` (or
`--player`) picks one player by name if you have several, `--once` prints
what is playing and stops, and `--dry-run` prints the dome commands
instead of sending them. To run it at boot, put the address in
`/etc/default/hemisphere-nowplaying` (as `HEOS_HOST=...`), not in the repo:

```bash
sudo cp tools/nowplaying/nowplaying.service /etc/systemd/system/hemisphere-nowplaying.service
sudo systemctl daemon-reload
sudo systemctl enable --now hemisphere-nowplaying
journalctl -u hemisphere-nowplaying -f
```

**Control commands** it uses, for anyone writing their own feed (UDP on
`127.0.0.1:5005`, one per packet): `palette RRGGBB RRGGBB RRGGBB RRGGBB`
(brightest first, or `palette off`), `np <serial>` (new `now.txt` and
`art.rgba` are ready), `progress <pos_ms> <dur_ms> <play|pause|stop>`,
`volume <0..100> <on|off>` (the level, and muted or not), `mode
<off|on_all|on_one> <on|off>` (repeat and shuffle), and `scene <n> <by>`, where `by` labels who changed the scene (`state` reports
it as `by`, with `since` in seconds). Scenes read it all with
`dome_nowplaying()` in `dome/dome.h`.

**Testing without the amp.** `tools/nowplaying/fakeheos.py` is a fake
HEOS device that plays made-up tracks with made-up covers drawn at run
time; point the tool at it with `HEOS_HOST=127.0.0.1:1255`. The tests use
only the fake and recorded, anonymised answers, and fail if anything
tries to reach the network:

```bash
venv/bin/pip install -r tools/nowplaying/requirements-dev.txt
venv/bin/python -m pytest tools/nowplaying/tests
```

`--record DIR --seconds N` saves what your amp says as new test fixtures,
with player names, addresses, serial numbers, model, network and any
account details replaced, then checks the file for each of those values
and prints found or not found.

**Credit.** The protocol follows the HEOS CLI Protocol Specification
published by Denon (HEOS is a trademark of its owners). No HEOS code is
included.
