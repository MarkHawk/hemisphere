"""A read-only HEOS CLI client.

HEOS devices (Denon, Marantz) take commands on TCP port 1255: one line
per command, "heos://group/command?key=value&...\\r\\n", and answer with
one JSON object per line. After system/register_for_change_events a
connection also receives events ("event/...") whenever the player
changes.

READ ONLY. The client refuses, before sending anything, every command
not in ALLOWED: nothing here can play, pause, skip, change the volume or
the input. register_for_change_events only changes what this connection
is told, not the amp.
"""
import json
import re
import socket
import time
import urllib.parse

PORT = 1255

RANGE = re.compile(r"^\d{1,3},\d{1,3}$")    # get_queue: first,last

# the only commands this client will send, and the values they may take:
# None = any, a set = one of these, a pattern = must match it. Every one
# only reads. Add to this list only after checking with the owner.
ALLOWED = {
    "system/register_for_change_events": {"enable": {"on", "off"}},
    "system/heart_beat": {},
    "player/get_players": {},
    "player/get_now_playing_media": {"pid": None},
    "player/get_play_state": {"pid": None},
    "player/get_volume": {"pid": None},
    "player/get_mute": {"pid": None},
    "player/get_play_mode": {"pid": None},
    "player/get_queue": {"pid": None, "range": RANGE},
}
OPTIONAL = {"player/get_queue": {"range"}}  # parameters that may be left out


class HeosError(Exception):
    pass


class NotAllowed(HeosError):
    """A command outside ALLOWED: it was not sent."""


class Disconnected(HeosError):
    pass


def build(command, **params):
    """The line to send for a command, after checking it is allowed."""
    if command not in ALLOWED:
        raise NotAllowed(f"{command} is not a read-only command")
    rules = ALLOWED[command]
    for k, v in params.items():
        if k not in rules:
            raise NotAllowed(f"{command} does not take {k}")
        rule = rules[k]
        if isinstance(rule, re.Pattern):
            ok = bool(rule.match(str(v)))
        else:
            ok = rule is None or str(v) in rule
        if not ok:
            raise NotAllowed(f"{command} {k}={v} is not allowed")
    for k in rules:
        if k not in params and k not in OPTIONAL.get(command, ()):
            raise HeosError(f"{command} needs {k}")
    # commas stay as they are: the amp does not decode %2C (get_queue's range)
    query = urllib.parse.urlencode({k: str(v) for k, v in params.items()}, safe=",")
    return f"heos://{command}" + (f"?{query}" if query else "") + "\r\n"


def parse_message(msg):
    """'pid=1&state=play' -> {'pid': '1', 'state': 'play'}. HEOS percent-
    encodes & = and % inside values; a part with no = (such as "command
    under process") becomes a key with an empty value."""
    out = {}
    for part in (msg or "").split("&"):
        if not part:
            continue
        k, eq, v = part.partition("=")
        out[urllib.parse.unquote(k)] = urllib.parse.unquote(v) if eq else ""
    return out


def parse_line(line):
    """One line from the device -> a dict with command, result, message
    (parsed), payload and is_event."""
    obj = json.loads(line)
    h = obj.get("heos", {})
    cmd = h.get("command", "")
    return {
        "command": cmd,
        "result": h.get("result", ""),
        "message": parse_message(h.get("message", "")),
        "payload": obj.get("payload"),
        "is_event": cmd.startswith("event/"),
        "raw": obj,
    }


def split_host(host, default_port=PORT):
    """'amp' or 'amp:1255' or '[::1]:1255' -> (host, port)."""
    host = host.strip()
    if host.startswith("["):
        h, _, rest = host[1:].partition("]")
        return h, int(rest[1:]) if rest.startswith(":") else default_port
    if host.count(":") == 1:
        h, p = host.split(":")
        return h, int(p)
    return host, default_port


class Connection:
    """One TCP connection to the device, line by line."""

    def __init__(self, host, port=PORT, timeout=5.0, tap=None):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""
        self.pending = []          # events read while waiting for a reply
        self.tap = tap             # tap(direction, text): for --record

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def send(self, command, **params):
        line = build(command, **params)
        if self.tap:
            self.tap("sent", line.strip())
        try:
            self.sock.sendall(line.encode())
        except OSError as e:
            raise Disconnected(str(e)) from e

    def read(self, timeout):
        """The next message, or None if nothing came within timeout."""
        deadline = time.monotonic() + timeout
        while b"\r\n" not in self.buf:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                return None
            except OSError as e:
                raise Disconnected(str(e)) from e
            if not data:
                raise Disconnected("the device closed the connection")
            self.buf += data
        raw, self.buf = self.buf.split(b"\r\n", 1)
        text = raw.decode("utf-8", "replace").strip()
        if not text:
            return self.read(max(0.0, deadline - time.monotonic()))
        if self.tap:
            self.tap("received", text)
        return parse_line(text)

    def request(self, command, timeout=5.0, **params):
        """Send a command and wait for its reply. Events that arrive
        meanwhile are kept for next_event()."""
        self.send(command, **params)
        deadline = time.monotonic() + timeout
        while True:
            m = self.read(max(0.0, deadline - time.monotonic()))
            if m is None:
                raise HeosError(f"no reply to {command}")
            if m["is_event"]:
                self.pending.append(m)
                continue
            if m["command"] != command:
                continue
            if "command under process" in m["message"]:
                continue                      # the real reply follows
            if m["result"] != "success":
                raise HeosError(f"{command} failed: {m['message'].get('text', m['message'])}")
            return m

    def next_event(self, timeout):
        if self.pending:
            return self.pending.pop(0)
        while True:
            m = self.read(timeout)
            if m is None or m["is_event"]:
                return m


class Client:
    """The two connections nowplaying uses: one for questions, one that
    only listens for events (as the HEOS CLI spec recommends)."""

    def __init__(self, host, port=PORT, timeout=5.0, tap=None):
        self.query = Connection(host, port, timeout, tap)
        try:
            self.events = Connection(host, port, timeout, tap)
            self.events.request("system/register_for_change_events", enable="on")
        except Exception:
            self.query.close()
            raise
        self.last_beat = time.monotonic()

    def close(self):
        self.query.close()
        self.events.close()

    def players(self):
        return self.query.request("player/get_players")["payload"] or []

    def now_playing(self, pid):
        return self.query.request("player/get_now_playing_media", pid=pid)["payload"] or {}

    def play_state(self, pid):
        return self.query.request("player/get_play_state", pid=pid)["message"].get("state", "stop")

    def volume(self, pid):
        """(level 0..100, muted)"""
        m = self.query.request("player/get_volume", pid=pid)["message"]
        mute = self.query.request("player/get_mute", pid=pid)["message"].get("state") == "on"
        try:
            return int(m.get("level", 0)), mute
        except ValueError:
            return 0, mute

    def play_mode(self, pid):
        """(repeat: off, on_all or on_one; shuffle: on or off)"""
        m = self.query.request("player/get_play_mode", pid=pid)["message"]
        return m.get("repeat", "off"), m.get("shuffle", "off")

    def queue(self, pid, first=0, last=49):
        return self.query.request("player/get_queue", pid=pid,
                                  range=f"{first},{last}")["payload"] or []

    def heartbeat(self, every=30.0):
        """Keep the question connection alive."""
        if time.monotonic() - self.last_beat >= every:
            self.query.request("system/heart_beat")
            self.last_beat = time.monotonic()

    def next_event(self, timeout=1.0):
        """The next event as {'event': name, **message}, or None."""
        m = self.events.next_event(timeout)
        if m is None:
            return None
        return dict(m["message"], event=m["command"][len("event/"):])


def choose_player(players, want=None):
    """The player to follow: the one named (by name, pid or model,
    ignoring case), else the only one, else the first."""
    if not players:
        return None
    if want:
        w = str(want).strip().lower()
        for p in players:
            if w in (str(p.get("name", "")).lower(), str(p.get("pid", "")),
                     str(p.get("model", "")).lower()):
                return p
        return None
    return players[0]


SSDP_ST = "urn:schemas-denon-com:device:ACT-Denon:1"


def discover(timeout=3.0, target=("239.255.255.250", 1900)):
    """Find HEOS devices on the network with an SSDP search (read only:
    a question every UPnP device answers). Returns their addresses."""
    msg = ("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
           f"MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: {SSDP_ST}\r\n\r\n").encode()
    found = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(0.5)
        s.sendto(msg, target)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                data, addr = s.recvfrom(4096)
            except socket.timeout:
                continue
            if SSDP_ST.encode() in data and addr[0] not in found:
                found.append(addr[0])
    return found
