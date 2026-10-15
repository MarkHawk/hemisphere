"""Hemisphere playlist: show scenes (and their styles) one after another.

Runs in a background thread inside the web remote, so it keeps going with
no browser open. The playlist is kept in playlist.json in the hemisphere
folder (or wherever HEMISPHERE_PLAYLIST points) and looks like this:

    {
      "enabled": true,          run it
      "order": "in_turn",       "in_turn", "shuffle" (each once per round)
                                or "random" (any, never the same twice)
      "seconds": 60,            how long each item stays up
      "vary": 0,                plus or minus this many seconds, at random
      "hold": 300,              after you pick something by hand, wait this
                                long before carrying on (0 = don't wait)
      "items": [
        {"scene": 10, "pick": 2, "seconds": 0, "on": true},
        ...                     pick = the style (or view, or theme) to
                                show, null to leave it as it is; seconds 0
                                = use the default; on false = skip it
      ]
    }
"""
import json
import os
import pathlib
import random
import threading
import time

ORDERS = ("in_turn", "shuffle", "random")
DEFAULT = {"enabled": False, "order": "in_turn", "seconds": 60, "vary": 0, "hold": 300, "items": []}


def _num(v, lo, hi, default):
    try:
        v = float(v)
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, v))


def choice_name(hint, n):
    """The name of choice n in a hint such as "0 football, 1 basketball (N)"."""
    for part in hint.replace("(N)", "").split(","):
        bits = part.strip().split(" ", 1)
        if len(bits) == 2 and bits[0] == str(n):
            return bits[1].strip()
    return ""


def clean(cfg):
    """Check a playlist from the web page, keeping only what makes sense."""
    out = dict(DEFAULT)
    if not isinstance(cfg, dict):
        return out
    out["enabled"] = bool(cfg.get("enabled", False))
    out["order"] = cfg.get("order") if cfg.get("order") in ORDERS else "in_turn"
    out["seconds"] = _num(cfg.get("seconds"), 5, 86400, 60)
    out["vary"] = _num(cfg.get("vary"), 0, 3600, 0)
    out["hold"] = _num(cfg.get("hold"), 0, 86400, 300)
    items = []
    for it in cfg.get("items", [])[:500]:
        if not isinstance(it, dict):
            continue
        try:
            scene = int(it.get("scene"))
        except (TypeError, ValueError):
            continue
        pick = it.get("pick")
        pick = None if pick is None or pick == "" else int(_num(pick, 0, 99, 0))
        items.append({"scene": scene, "pick": pick,
                      "seconds": _num(it.get("seconds"), 0, 86400, 0),
                      "on": bool(it.get("on", True))})
    out["items"] = items
    return out


class Playlist:
    def __init__(self, send, path):
        self.send = send                     # send(command) -> reply or None
        self.path = pathlib.Path(path)
        self.lock = threading.Lock()
        self.cfg = self.load()
        self.pos = -1                        # index into items of what's showing
        self.next_at = 0.0                   # when to move on (monotonic)
        self.hold_until = 0.0
        self.bag = []                        # shuffle: what's left this round
        self.catalog = None                  # scene names and pick settings
        self.history = []                    # for "previous"
        threading.Thread(target=self.run, daemon=True).start()

    # ---- the file -------------------------------------------------
    def load(self):
        try:
            return clean(json.loads(self.path.read_text()))
        except (OSError, ValueError):
            return dict(DEFAULT)

    def save(self):
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.cfg, indent=2))
        os.replace(tmp, self.path)

    def update(self, cfg):
        with self.lock:
            was = self.cfg["enabled"]
            self.cfg = clean(cfg)
            self.bag = []
            if self.cfg["enabled"] and not was:
                self.next_at = 0.0           # just started: show the first now
                self.hold_until = 0.0
            self.save()

    # ---- what the page shows --------------------------------------
    def get_catalog(self):
        reply = self.send("catalog")
        if reply:
            try:
                self.catalog = json.loads(reply)["scenes"]
            except (ValueError, KeyError):
                pass
        return self.catalog

    def status(self):
        with self.lock:
            now = time.monotonic()
            items = self.cfg["items"]
            cur = items[self.pos] if 0 <= self.pos < len(items) else None
            return {
                "running": self.cfg["enabled"],
                "held": max(0, round(self.hold_until - now)) if self.cfg["enabled"] else 0,
                "index": self.pos,
                "current": self.describe(cur) if cur else "",
                "next_in": max(0, round(self.next_at - now)) if self.cfg["enabled"] else None,
            }

    def describe(self, it):
        cat = self.catalog or []
        if not (0 <= it["scene"] < len(cat)):
            return f"scene {it['scene']}"
        sc = cat[it["scene"]]
        name = sc["name"].capitalize()
        if it["pick"] is not None and sc.get("pick"):
            return f"{name}: {choice_name(sc.get('hint', ''), it['pick']) or sc['pick'] + ' ' + str(it['pick'])}"
        return name

    # ---- moving along ---------------------------------------------
    def touch(self):
        """Someone picked something by hand: wait before carrying on."""
        with self.lock:
            if self.cfg["enabled"] and self.cfg["hold"] > 0:
                self.hold_until = time.monotonic() + self.cfg["hold"]
                self.next_at = max(self.next_at, self.hold_until)

    def choose(self, step):
        items = self.cfg["items"]
        live = [i for i, it in enumerate(items) if it["on"]]
        if not live:
            return -1
        order = self.cfg["order"]
        if step < 0 and self.history:        # back to what was showing before
            self.history.pop()
            if self.history:
                return self.history.pop()
        if order == "in_turn" or step < 0:
            later = [i for i in live if i > self.pos] if step > 0 else [i for i in live if i < self.pos]
            if step > 0:
                return later[0] if later else live[0]
            return later[-1] if later else live[-1]
        if order == "random":
            choices = [i for i in live if i != self.pos] or live
            return random.choice(choices)
        if not self.bag:                     # shuffle: a fresh round
            self.bag = live[:]
            random.shuffle(self.bag)
            if len(self.bag) > 1 and self.bag[0] == self.pos:
                self.bag.append(self.bag.pop(0))
        return self.bag.pop(0)

    def show(self, i):
        it = self.cfg["items"][i]
        cat = self.catalog or self.get_catalog() or []
        if it["pick"] is not None and 0 <= it["scene"] < len(cat) and cat[it["scene"]].get("pick"):
            self.send(f"sset {it['scene']} {cat[it['scene']]['pick']} {it['pick']}")
        ok = self.send(f"scene {it['scene']}") is not None
        if ok:
            self.pos = i
            self.history = (self.history + [i])[-50:]
            secs = it["seconds"] or self.cfg["seconds"]
            secs += random.uniform(-self.cfg["vary"], self.cfg["vary"])
            self.next_at = time.monotonic() + max(5.0, secs)
        return ok

    def skip(self, step):
        """Next (step 1) or previous (step -1), now."""
        with self.lock:
            i = self.choose(step)
            if i >= 0:
                self.hold_until = 0.0
                self.show(i)

    def run(self):
        while True:
            time.sleep(0.5)
            with self.lock:
                if not self.cfg["enabled"] or not self.cfg["items"]:
                    continue
                now = time.monotonic()
                if now < self.next_at or now < self.hold_until:
                    continue
                i = self.choose(1)
                if i < 0 or not self.show(i):
                    self.next_at = now + 5.0     # hemisphere not running: try again soon
