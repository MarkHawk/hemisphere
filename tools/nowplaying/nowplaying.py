#!/usr/bin/env python3
"""Hemisphere now playing: what a HEOS amp is playing, on the dome.

Follows a HEOS player (Denon, Marantz) over its CLI, read only, and
passes on what it is playing:

  the cover    downloaded, cached and resized into data/nowplaying/
               (art.rgba, thumb.rgba), never committed
  the palette  four colours from the cover, for theme 8 (album)
  the details  title, artist, album, station, source: data/nowplaying/now.txt
  progress     position, length and play state, about once a second
  the rest     volume and mute, repeat and shuffle, and what the HEOS
               queue plays next (now.txt's next=), as they change

and switches the dome to the music when it starts, and back when it
stops (see switcher.py; --no-auto turns that off). Which source does
what is in sources.json.

    venv/bin/pip install -r tools/nowplaying/requirements.txt
    HEOS_HOST=<your amp's address> venv/bin/python tools/nowplaying/nowplaying.py

Options (environment variables in brackets):
    --host ADDR[:PORT]   the amp (HEOS_HOST). Left out: a network search
    --player NAME        which player, by name, pid or model (HEOS_PLAYER)
    --config FILE        source rules and timings (default sources.json)
    --no-auto            never change scene, just pass the music on
    --idle S, --hold S   override the idle and hold times, in seconds
    --dry-run            print the dome commands instead of sending them
    --once               print what is playing and stop
    --record DIR --seconds N
                         save what the amp says, with names, addresses,
                         serial numbers and accounts replaced, as test
                         fixtures; then report which private values
                         were found in the files (they should not be)

It never sends the amp anything that changes it: see heos.ALLOWED.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import sys
import threading
import time

import art
import heos
from domectl import Dome
from switcher import SHOWS, Switcher, classify, input_label, load_config

HERE = pathlib.Path(__file__).resolve().parent
KEEP_PALETTE = {"disc"}        # no cover expected: keep the colours there are
# what some radio streams send as the song: "256 kbps mp3", "128k AAC"
STREAM_INFO = re.compile(r"^\s*\d{2,4}\s*k(bps|bit/s|b/s|bit)?\b[\s\w/+-]*$", re.I)


def is_stream_info(text):
    return bool(STREAM_INFO.match(text or ""))


def next_up(queue, qid):
    """'Song - Artist' of the queue item after qid, or ''."""
    for i, q in enumerate(queue):
        if str(q.get("qid")) == str(qid) and i + 1 < len(queue):
            n = queue[i + 1]
            return " - ".join(x for x in (n.get("song"), n.get("artist")) if x)
    return ""


class NowPlaying:
    """The work, given a way to connect (so the tests can use the fake)."""

    def __init__(self, connect, dome, cfg, player=None, data_dir=None, log=print,
                 auto=True):
        self.connect = connect
        self.dome = dome
        self.cfg = cfg
        self.want = player
        self.data = pathlib.Path(data_dir or dome.data)
        self.cache = self.data / "cache"
        self.log = log
        cfg = dict(cfg, auto=cfg.get("auto", True) and auto)
        self.switcher = Switcher(dome, cfg, log=log)
        self.client = None
        self.pid = None
        self.player_name = ""
        self.media = {}
        self.track = None
        self.behaviour, self.source, self.audio_means = "default", "", False
        self.state = "stop"
        self.pos = self.dur = 0
        self.palette = None
        self.has_art = False
        self.info = {}
        self.sound = False
        self.level, self.mute = -1, False
        self.repeat, self.shuffle = "off", "off"
        self.next = ""
        # a radio station sends its details in pieces, a logo one minute
        # and none the next: keep what it gave while it is the same station
        self.radio = {"id": None, "station": "", "palette": None, "art": False}
        self.next_sound = 0.0
        self.next_sync = 0.0
        self.published = None

    # ---- what is playing ------------------------------------------
    def title_of(self, m):
        """The title to show; inputs come as '<player> - Coaxial In' and
        are shown by their label ("CD") or without the player's name."""
        label = input_label(m, self.cfg)
        if label:
            return label
        song = "" if is_stream_info(m.get("song")) else m.get("song")
        t = song or m.get("station") or ""
        if self.player_name and t.startswith(self.player_name + " - "):
            t = t[len(self.player_name) + 3:]
        return t

    def take_media(self, m):
        m = m or {}
        key = tuple(str(m.get(k, "")) for k in
                    ("song", "artist", "album", "station", "image_url", "mid", "sid"))
        if key == self.track:
            return
        self.track = key
        self.media = m
        self.behaviour, self.source, self.audio_means = classify(m, self.cfg)
        if self.behaviour not in KEEP_PALETTE:
            self.palette = None
        self.has_art = False
        url = m.get("image_url") or ""
        if url and self.behaviour in SHOWS:
            try:
                pal = art.prepare(art.fetch(url, self.cache), self.data)
                self.palette, self.has_art = art.hexes(pal), True
                art.prune(self.cache)
            except art.ArtError as e:
                self.log(f"No cover: {e}")
        # Spotify comes as type "station" with the song title as the
        # station: show a station only if it says something new, or for
        # inputs (where the name is all there is)
        station = m.get("station") or ""
        if station == m.get("song") and not str(m.get("mid", "")).startswith("inputs/"):
            station = ""
        if self.player_name and station.startswith(self.player_name + " - "):
            station = station[len(self.player_name) + 3:]
        station = input_label(m, self.cfg) or station
        if self.behaviour == "radio":
            station = self.remember_station(m, station)
        self.next = self.read_next()
        self.info = {"title": self.title_of(m), "artist": m.get("artist", ""),
                     "album": m.get("album", ""), "station": station,
                     "source": self.source, "behaviour": self.behaviour,
                     "next": self.next, "art": self.has_art}
        self.send_all()
        what = " / ".join(x for x in (self.info["title"], self.info["artist"]) if x)
        self.log(f"Now playing: {what or 'nothing'} ({self.source}, {self.behaviour}"
                 f"{', with cover' if self.has_art else ''})")

    def remember_station(self, m, station):
        """Fill gaps in a station's details from what it sent before."""
        sid = (str(m.get("sid", "")), str(m.get("album_id") or m.get("mid") or ""))
        r = self.radio
        if sid != r["id"]:
            r.update(id=sid, station="", palette=None, art=False)
        if station:
            r["station"] = station
        if self.has_art:
            r["palette"], r["art"] = self.palette, True
        elif r["art"]:                         # no logo this time: keep the last
            self.palette, self.has_art = r["palette"], True
        return station or r["station"]

    def read_next(self):
        """What the HEOS queue plays next (empty for Spotify Connect,
        radio and inputs, which have no HEOS queue)."""
        if self.client is None or self.pid is None:
            return ""
        try:
            return next_up(self.client.queue(self.pid), self.media.get("qid"))
        except heos.HeosError:
            return ""

    def read_levels(self):
        try:
            self.level, self.mute = self.client.volume(self.pid)
            self.repeat, self.shuffle = self.client.play_mode(self.pid)
        except heos.HeosError as e:
            self.log(f"Could not read volume or play mode: {e}")

    def send_all(self):
        """Everything the dome should know, again (also after it restarts)."""
        self.dome.palette(self.palette)
        self.published = self.dome.publish(self.info)
        self.dome.progress(self.pos, self.dur, self.state)
        if self.level >= 0:
            self.dome.volume(self.level, self.mute)
        self.dome.mode(self.repeat, self.shuffle)

    def playing(self):
        if self.state == "play":
            return True
        return self.audio_means and self.sound

    # ---- events -----------------------------------------------------
    def on_event(self, e):
        name = e.get("event")
        if name == "players_changed":
            self.choose_player()
            return
        if str(e.get("pid", "")) != str(self.pid):
            return
        if name == "player_now_playing_changed":
            self.take_media(self.client.now_playing(self.pid))
        elif name == "player_state_changed":
            self.state = e.get("state", "stop")
            self.dome.progress(self.pos, self.dur, self.state)
        elif name == "player_volume_changed":
            try:
                self.level = int(e.get("level", self.level))
            except ValueError:
                return
            self.mute = e.get("mute") == "on"
            self.dome.volume(self.level, self.mute)
        elif name in ("repeat_mode_changed", "shuffle_mode_changed"):
            self.repeat = e.get("repeat", self.repeat)
            self.shuffle = e.get("shuffle", self.shuffle)
            self.dome.mode(self.repeat, self.shuffle)
        elif name == "player_queue_changed":
            nxt = self.read_next()
            if nxt != self.next:
                self.next = self.info["next"] = nxt
                self.published = self.dome.publish(self.info)
        elif name == "player_now_playing_progress":
            try:
                self.pos, self.dur = int(e.get("cur_pos", 0)), int(e.get("duration", 0))
            except ValueError:
                return
            self.dome.progress(self.pos, self.dur, self.state)

    def choose_player(self):
        players = self.client.players()
        p = heos.choose_player(players, self.want)
        if p is None:
            raise heos.HeosError(f"no player {self.want!r} among "
                                 f"{[x.get('name') for x in players]}")
        if p["pid"] != self.pid:
            self.pid, self.player_name = p["pid"], str(p.get("name", ""))
            self.log(f"Following {self.player_name} ({p.get('model', '')})")
            self.track = None

    # ---- each second -------------------------------------------------
    def tick(self, now):
        if self.audio_means and self.state != "play" and now >= self.next_sound:
            self.next_sound = now + 2.0
            st = self.dome.state() or {}
            self.sound = float((st.get("audio") or {}).get("live", 0)) > 0.5
        if now >= self.next_sync:                 # hemisphere restarted?
            self.next_sync = now + 10.0
            st = self.dome.state()
            if st and self.published is not None and \
                    (st.get("nowplaying") or {}).get("serial") != self.published:
                self.send_all()
        self.switcher.update(now, self.playing(), self.behaviour)

    # ---- the loop ------------------------------------------------------
    def session(self, stop):
        self.client = self.connect()
        try:
            self.pid = None
            self.choose_player()
            self.state = self.client.play_state(self.pid)
            self.read_levels()
            self.take_media(self.client.now_playing(self.pid))
            self.dome.progress(self.pos, self.dur, self.state)
            while not stop.is_set():
                e = self.client.next_event(0.5)
                if e:
                    self.on_event(e)
                self.client.heartbeat()
                self.tick(time.monotonic())
        finally:
            self.client.close()

    def run(self, stop=None):
        stop = stop or threading.Event()
        wait = 2.0
        while not stop.is_set():
            try:
                self.session(stop)
                wait = 2.0
            except (heos.HeosError, OSError) as e:
                self.log(f"Amp: {e}; trying again in {wait:.0f} s")
                self.state = "stop"
                self.dome.progress(self.pos, self.dur, "stop")
                end = time.monotonic() + wait
                while time.monotonic() < end and not stop.is_set():
                    self.tick(time.monotonic())   # the idle timer keeps running
                    stop.wait(0.5)
                wait = min(60.0, wait * 2)


# ---- recording fixtures --------------------------------------------
PLACE = {"name": "Test Amp", "ip": "192.0.2.10", "serial": "TEST0000000",
         "model": "Test Model", "network": "test-network", "pid": 1234567}
ACCOUNT_KEYS = {"un", "pw", "username", "user", "account", "email", "signed_in_user"}
IPV4 = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b")
MAC = re.compile(r"\b[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}\b")
EMAIL = re.compile(r"[\w.+-]+@[\w-]+\.[\w.-]+")


class Scrubber:
    def __init__(self):
        self.secrets = {}          # original value -> what it is

    def learn(self, players, host):
        for p in players or []:
            for k in ("name", "ip", "serial", "model", "network"):
                v = str(p.get(k, "")).strip()
                if len(v) >= 3 and v != str(PLACE[k]):
                    self.secrets[v] = k
            if p.get("pid") is not None and p["pid"] != PLACE["pid"]:
                self.secrets[str(p["pid"])] = "pid"
            model = str(p.get("model", "")).split()
            if len(model) > 1 and len(model[-1]) >= 4:
                self.secrets[model[-1]] = "model"
        host = heos.split_host(host)[0] if host else ""
        if host and host not in ("127.0.0.1", "localhost", PLACE["ip"]):
            self.secrets[host] = "ip"
        # a value that is part of a placeholder is a placeholder already
        for v in [v for v in self.secrets if any(v in str(x) for x in PLACE.values())]:
            del self.secrets[v]

    def text(self, s):
        for v in sorted(self.secrets, key=len, reverse=True):
            s = s.replace(v, str(PLACE[self.secrets[v]]))
        s = IPV4.sub(PLACE["ip"], s)
        s = MAC.sub("00:00:00:00:00:00", s)
        return EMAIL.sub("someone@example.invalid", s)

    def obj(self, o, key=None):
        if isinstance(o, dict):
            return {k: self.obj(v, k) for k, v in o.items()}
        if isinstance(o, list):
            return [self.obj(v) for v in o]
        if key in ACCOUNT_KEYS:
            return "<account>"
        if key in PLACE and key != "pid":
            return PLACE[key]
        if key == "pid":
            return PLACE["pid"]
        if isinstance(o, str):
            return self.text(o)
        return o

    def scan(self, path):
        """[(what, found)] for every private value, in the written file."""
        body = pathlib.Path(path).read_text()
        out = [(f"{what} ({len(v)} chars)", v in body) for v, what in self.secrets.items()
               if what != "pid" or len(v) >= 5]
        out.append(("any e-mail address", bool(EMAIL.search(body))))
        out.append(("any MAC address", bool(MAC.search(body.replace("00:00:00:00:00:00", "")))))
        ips = set(IPV4.findall(body)) - {PLACE["ip"]}
        out.append(("any other IPv4 address", bool(ips)))
        return out


def record(host, player, out_dir, seconds, log=print):
    lines = []
    client = heos.Client(*heos.split_host(host),
                         tap=lambda d, t: lines.append({"direction": d, "line": t}))
    sc = Scrubber()
    try:
        players = client.players()
        sc.learn(players, host)
        p = heos.choose_player(players, player)
        if p is None:
            raise heos.HeosError("player not found")
        client.play_state(p["pid"])
        client.now_playing(p["pid"])
        log(f"Recording for {seconds} s: change source, play, pause...")
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            e = client.next_event(1.0)
            if e and e.get("event") == "player_now_playing_changed":
                client.now_playing(p["pid"])
            elif e and e.get("event") == "player_state_changed":
                client.play_state(p["pid"])
            client.heartbeat()
    finally:
        client.close()
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"recorded_{datetime.date.today():%Y%m%d_}{int(time.time()) % 100000}.jsonl"
    with path.open("w") as f:
        for l in lines:
            try:
                body = sc.obj(json.loads(l["line"]))
            except ValueError:
                body = sc.text(l["line"])
            f.write(json.dumps({"direction": l["direction"], "line": body}) + "\n")
    log(f"Wrote {len(lines)} lines to {path}")
    log("Check for private values:")
    bad = False
    for what, found in sc.scan(path):
        log(f"  {what:32} {'FOUND' if found else 'not found'}")
        bad |= found
    return path, not bad


# ---- start ------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="Hemisphere now playing (HEOS, read only)")
    ap.add_argument("--host", default=os.environ.get("HEOS_HOST"))
    ap.add_argument("--player", default=os.environ.get("HEOS_PLAYER"))
    ap.add_argument("--config", default=None)
    ap.add_argument("--no-auto", action="store_true")
    ap.add_argument("--idle", type=float, metavar="SECONDS",
                    help="hand the dome back after this long paused (for testing)")
    ap.add_argument("--hold", type=float, metavar="SECONDS",
                    help="leave the dome alone this long after a change by hand")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--record", metavar="DIR")
    ap.add_argument("--seconds", type=int, default=120)
    args = ap.parse_args(argv)
    sys.stdout.reconfigure(line_buffering=True)     # logs show at once under systemd
    host = args.host
    if not host:
        print("No HEOS_HOST given: searching the network...")
        found = heos.discover()
        if not found:
            print("No HEOS device found. Pass its address: --host or HEOS_HOST")
            return 1
        host = found[0]
        print(f"Found HEOS at {host}")
    if args.record:
        _, clean = record(host, args.player, args.record, args.seconds)
        return 0 if clean else 2
    if args.once:
        c = heos.Client(*heos.split_host(host))
        try:
            p = heos.choose_player(c.players(), args.player)
            if p is None:
                print("Player not found")
                return 1
            print(json.dumps({"player": p.get("name"), "state": c.play_state(p["pid"]),
                              "now": c.now_playing(p["pid"])}, indent=2))
        finally:
            c.close()
        return 0
    cfg = load_config(args.config)
    if args.idle is not None:
        cfg["idle_minutes"] = args.idle / 60
    if args.hold is not None:
        cfg["hold_minutes"] = args.hold / 60
    dome = Dome(dry_run=args.dry_run)
    np = NowPlaying(lambda: heos.Client(*heos.split_host(host)), dome, cfg,
                    player=args.player, auto=not args.no_auto)
    try:
        np.run()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
