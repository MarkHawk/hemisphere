#!/usr/bin/env python3
"""A fake HEOS device, for tests and demos without the amp.

Speaks enough of the HEOS CLI (TCP, one JSON line per message) for
nowplaying.py: every read-only command in heos.ALLOWED (players, now
playing, play state, volume, mute, play mode, queue, heart beat and
register_for_change_events), plus the change events. Any other
command is answered with a failure and logged, so a test can prove the
tool never sent one. It also serves made-up cover pictures over HTTP
(drawn with Pillow at run time: no real album art anywhere).

    venv/bin/python tools/nowplaying/fakeheos.py               demo on 127.0.0.1:1255
    venv/bin/python tools/nowplaying/fakeheos.py --port 11255 --art-port 18080

then, in another terminal,

    HEOS_HOST=127.0.0.1:1255 venv/bin/python tools/nowplaying/nowplaying.py

The demo plays three made-up tracks from a streaming service, then the
phono input, then pauses, then the TV input, round and round.
"""
import argparse
import http.server
import io
import json
import socket
import socketserver
import threading
import time
import urllib.parse

PID = 1234567
PLAYER = {"name": "Test Amp", "pid": PID, "model": "Test Model", "version": "1.0.0",
          "ip": "192.0.2.10", "network": "wired", "lineout": 0, "serial": "TEST0000000"}


def _line(obj):
    return (json.dumps(obj, separators=(",", ":")) + "\r\n").encode()


def _msg(**kv):
    return "&".join(f"{k}={urllib.parse.quote(str(v), safe='')}" for k, v in kv.items())


class FakeHeos:
    """The fake device. start() it, then change what it is playing with
    play(), set_state() and progress(); each sends the matching events to
    every connection that registered for them."""

    def __init__(self, host="127.0.0.1", port=0):
        self.players = [dict(PLAYER)]
        self.media = {}
        self.state = "stop"
        self.level, self.mute = 20, "off"
        self.repeat, self.shuffle = "off", "off"
        self.queue = []
        self.commands = []                  # every command received, as sent
        self.refused = []                   # commands it does not know
        self.lock = threading.Lock()
        self.clients = []                   # [socket, registered]
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.listen(8)
        self.host, self.port = self.sock.getsockname()[:2]
        self.running = False

    # ---- running ---------------------------------------------------
    def start(self):
        self.running = True
        threading.Thread(target=self._accept, daemon=True).start()
        return self

    def stop(self):
        self.running = False
        try:
            self.sock.close()
        except OSError:
            pass
        with self.lock:
            for c in self.clients:
                try:
                    c[0].close()
                except OSError:
                    pass
            self.clients = []

    def drop_clients(self):
        """Close every connection, as the amp does going into standby."""
        with self.lock:
            for c in self.clients:
                try:
                    c[0].shutdown(socket.SHUT_RDWR)
                    c[0].close()
                except OSError:
                    pass
            self.clients = []

    def _accept(self):
        while self.running:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            entry = [conn, False]
            with self.lock:
                self.clients.append(entry)
            threading.Thread(target=self._serve, args=(entry,), daemon=True).start()

    def _serve(self, entry):
        conn = entry[0]
        buf = b""
        while self.running:
            try:
                data = conn.recv(4096)
            except OSError:
                break
            if not data:
                break
            buf += data
            while b"\r\n" in buf:
                raw, buf = buf.split(b"\r\n", 1)
                reply = self._handle(raw.decode("utf-8", "replace").strip(), entry)
                if reply is not None:
                    self._send(conn, reply)
        with self.lock:
            if entry in self.clients:
                self.clients.remove(entry)

    def _send(self, conn, obj):
        try:
            conn.sendall(_line(obj))
        except OSError:
            pass

    # ---- the commands it knows -------------------------------------
    def _handle(self, raw, entry):
        self.commands.append(raw)
        if not raw.startswith("heos://"):
            return None
        path, _, query = raw[7:].partition("?")
        params = dict(urllib.parse.parse_qsl(query))
        with self.lock:
            pid_ok = str(params.get("pid")) in {str(p["pid"]) for p in self.players}
            if path == "system/heart_beat":
                return {"heos": {"command": path, "result": "success", "message": ""}}
            if path == "system/register_for_change_events":
                entry[1] = params.get("enable") == "on"
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(enable=params.get("enable", ""))}}
            if path == "player/get_players":
                return {"heos": {"command": path, "result": "success", "message": ""},
                        "payload": [dict(p) for p in self.players]}
            if path in ("player/get_play_state", "player/get_now_playing_media") and not pid_ok:
                return {"heos": {"command": path, "result": "fail",
                                 "message": "eid=2&text=ID Not Valid"}}
            if path in ("player/get_volume", "player/get_mute", "player/get_play_mode",
                        "player/get_queue") and not pid_ok:
                return {"heos": {"command": path, "result": "fail",
                                 "message": "eid=2&text=ID Not Valid"}}
            if path == "player/get_volume":
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(pid=params["pid"], level=self.level)}}
            if path == "player/get_mute":
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(pid=params["pid"], state=self.mute)}}
            if path == "player/get_play_mode":
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(pid=params["pid"], repeat=self.repeat,
                                                 shuffle=self.shuffle)}}
            if path == "player/get_queue":
                a, b = (int(x) for x in params.get("range", "0,99").split(","))
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(pid=params["pid"], range=f"{a},{b}")},
                        "payload": [dict(q) for q in self.queue[a:b + 1]]}
            if path == "player/get_play_state":
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(pid=params["pid"], state=self.state)}}
            if path == "player/get_now_playing_media":
                return {"heos": {"command": path, "result": "success",
                                 "message": _msg(pid=params["pid"])},
                        "payload": dict(self.media), "options": []}
        self.refused.append(raw)
        return {"heos": {"command": path, "result": "fail",
                         "message": "eid=1&text=Unrecognized Command"}}

    # ---- events -----------------------------------------------------
    def _event(self, name, **kv):
        msg = {"heos": {"command": f"event/{name}", "message": _msg(**kv)}}
        with self.lock:
            targets = [c[0] for c in self.clients if c[1]]
        for conn in targets:
            self._send(conn, msg)

    def play(self, media, state="play"):
        """Start something new playing."""
        with self.lock:
            self.media = dict(media)
            self.state = state
        self._event("player_now_playing_changed", pid=PID)
        self._event("player_state_changed", pid=PID, state=state)

    def set_state(self, state):
        with self.lock:
            self.state = state
        self._event("player_state_changed", pid=PID, state=state)

    def set_volume(self, level, mute="off"):
        with self.lock:
            self.level, self.mute = level, mute
        self._event("player_volume_changed", pid=PID, level=level, mute=mute)

    def set_mode(self, repeat=None, shuffle=None):
        if repeat is not None:
            self.repeat = repeat
            self._event("repeat_mode_changed", pid=PID, repeat=repeat)
        if shuffle is not None:
            self.shuffle = shuffle
            self._event("shuffle_mode_changed", pid=PID, shuffle=shuffle)

    def set_queue(self, items):
        with self.lock:
            self.queue = [dict(q) for q in items]
        self._event("player_queue_changed", pid=PID)

    def progress(self, cur_ms, dur_ms):
        self._event("player_now_playing_progress", pid=PID, cur_pos=int(cur_ms),
                    duration=int(dur_ms))


# ---- made-up cover pictures ----------------------------------------
def cover_png(seed, size=600):
    """A made-up cover: a gradient and a few shapes from the seed."""
    from PIL import Image, ImageDraw
    import random
    rnd = random.Random(seed)
    cols = [tuple(rnd.randrange(30, 256) for _ in range(3)) for _ in range(4)]
    img = Image.new("RGB", (size, size), cols[0])
    d = ImageDraw.Draw(img)
    for y in range(size):
        t = y / size
        d.line([(0, y), (size, y)], fill=tuple(int(cols[0][k] * (1 - t) + cols[1][k] * t)
                                               for k in range(3)))
    for i in range(5):
        r = rnd.randrange(size // 10, size // 3)
        x, y = rnd.randrange(size), rnd.randrange(size)
        d.ellipse([x - r, y - r, x + r, y + r], fill=cols[2 + i % 2])
    out = io.BytesIO()
    img.save(out, "PNG")
    return out.getvalue()


class ArtServer:
    """HTTP on 127.0.0.1: /cover/<seed>.png a made-up cover,
    /text a page that is not a picture, /big a file over any sensible
    limit, /missing a 404."""

    def __init__(self, host="127.0.0.1", port=0):
        self.hits = []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                outer.hits.append(self.path)
                if self.path.startswith("/cover/"):
                    seed = self.path[7:].split(".")[0]
                    body, ctype = cover_png(seed), "image/png"
                elif self.path == "/text":
                    body, ctype = b"<html>not a picture</html>", "text/html"
                elif self.path == "/big":
                    body, ctype = b"\0" * (6 * 1024 * 1024), "image/png"
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
            daemon_threads = True
            allow_reuse_address = True

        self.httpd = Server((host, port), Handler)
        self.port = self.httpd.server_address[1]

    def url(self, path):
        return f"http://127.0.0.1:{self.port}{path}"

    def start(self):
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        return self

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()


# ---- the demo -------------------------------------------------------
def demo_steps(art):
    """(media, state, seconds) in turn: made-up music only."""
    song = lambda n, title, artist, album: {
        "type": "song", "song": title, "artist": artist, "album": album,
        "image_url": art.url(f"/cover/{n}.png"), "mid": f"demo:{n}", "qid": n,
        "sid": 4, "album_id": f"demo-album-{n}"}
    return [
        (song(1, "Glass Orbit", "The Test Signals", "Calibration"), "play", 40),
        (song(2, "Northern Rim", "The Test Signals", "Calibration"), "play", 40),
        (song(3, "Lens Flare Lullaby", "Fisheye Quartet", "Wide Angle"), "play", 40),
        ({"type": "station", "song": "", "station": "Phono", "album": "", "artist": "",
          "image_url": "", "mid": "inputs/phono", "sid": 1027}, "play", 30),
        ({"type": "station", "song": "", "station": "Phono", "album": "", "artist": "",
          "image_url": "", "mid": "inputs/phono", "sid": 1027}, "pause", 30),
        ({"type": "station", "song": "", "station": "TV Audio", "album": "", "artist": "",
          "image_url": "", "mid": "inputs/optical_in_1", "sid": 1027}, "play", 30),
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=1255)
    ap.add_argument("--art-port", type=int, default=18080)
    ap.add_argument("--speed", type=float, default=1.0, help="run the demo faster (2 = twice)")
    args = ap.parse_args()
    art = ArtServer(port=args.art_port).start()
    heos = FakeHeos(port=args.port).start()
    print(f"Fake HEOS on 127.0.0.1:{heos.port}, covers on 127.0.0.1:{art.port}")
    try:
        while True:
            for media, state, secs in demo_steps(art):
                label = media.get("song") or media.get("station")
                print(f"  {state:5} {label}")
                heos.play(media, state)
                dur = secs * 1000
                for t in range(secs):
                    if state == "play":
                        heos.progress(t * 1000, dur)
                    time.sleep(1.0 / args.speed)
    except KeyboardInterrupt:
        pass
    finally:
        heos.stop()
        art.stop()


if __name__ == "__main__":
    main()
