"""Talking to the running hemisphere program, and the files it reads.

Commands go over the control channel (UDP on 127.0.0.1, port 5005 or
DOME_CTL_PORT), one per packet, one reply each. The track's details are
written to data/nowplaying/now.txt (key=value lines, plain ASCII for the
dome's 5x7 font) next to art.rgba, then "np <serial>" tells the program
to read them.
"""
import json
import os
import pathlib
import socket
import time
import unicodedata

HERE = pathlib.Path(__file__).resolve().parent
DATA = HERE.parent.parent / "data" / "nowplaying"


def ascii_text(s, limit=120):
    """Beyoncé -> Beyonce; anything the font lacks is dropped."""
    s = unicodedata.normalize("NFKD", str(s or ""))
    s = "".join(c if 32 <= ord(c) < 127 else " " if c.isspace() else "" for c in s)
    return " ".join(s.split())[:limit]


class Dome:
    def __init__(self, port=None, data_dir=None, dry_run=False, timeout=1.0, log=print):
        self.addr = ("127.0.0.1", int(port or os.environ.get("DOME_CTL_PORT", "5005")))
        self.data = pathlib.Path(data_dir or DATA)
        self.dry_run = dry_run
        self.timeout = timeout
        self.log = log
        self.serial = int(time.time() * 10) % 2_000_000_000   # rises across restarts

    def send(self, command):
        """Send one command; the reply, or None if hemisphere is not running."""
        if self.dry_run:
            self.log(f"[dry run] {command}")
            return "{}" if command in ("state", "catalog") else "ok"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.settimeout(self.timeout)
            try:
                s.sendto(command.encode(), self.addr)
                return s.recvfrom(65535)[0].decode("utf-8", "replace")
            except OSError:
                return None

    def _json(self, command):
        reply = self.send(command)
        try:
            return json.loads(reply) if reply else None
        except ValueError:
            return None

    def state(self):
        return self._json("state")

    def catalog(self):
        return self._json("catalog")

    def scene(self, n, by):
        return self.send(f"scene {int(n)} {by}")

    def sset(self, scene, name, value):
        return self.send(f"sset {int(scene)} {name} {value:g}")

    def palette(self, hexes):
        return self.send("palette " + (" ".join(hexes) if hexes else "off"))

    def volume(self, level, mute):
        level = max(0, min(100, int(level)))
        return self.send(f"volume {level} {'on' if mute else 'off'}")

    def mode(self, repeat, shuffle):
        repeat = repeat if repeat in ("off", "on_all", "on_one") else "off"
        return self.send(f"mode {repeat} {'on' if shuffle == 'on' else 'off'}")

    def progress(self, pos_ms, dur_ms, state):
        state = state if state in ("play", "pause", "stop") else "stop"
        return self.send(f"progress {max(0, int(pos_ms))} {max(0, int(dur_ms))} {state}")

    def publish(self, info):
        """Write now.txt (info: title, artist, album, station, source,
        behaviour, next, art) and tell hemisphere. Returns the serial."""
        self.serial += 1
        lines = [f"serial={self.serial}"]
        for k in ("title", "artist", "album", "station", "source", "behaviour", "next"):
            lines.append(f"{k}={ascii_text(info.get(k, ''))}")
        lines.append(f"art={1 if info.get('art') else 0}")
        self.data.mkdir(parents=True, exist_ok=True)
        tmp = self.data / "now.tmp"
        tmp.write_text("\n".join(lines) + "\n")
        os.replace(tmp, self.data / "now.txt")
        self.send(f"np {self.serial}")
        return self.serial
