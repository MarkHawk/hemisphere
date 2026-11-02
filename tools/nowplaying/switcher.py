"""Which HEOS source does what, and when to switch the dome to the music.

classify() maps what the amp is playing to a behaviour, using the rules
in sources.json. Switcher decides when to change scene:

  take over    the amp has been playing for settle_seconds on a source
               that shows (album, radio, record, disc, default), and nobody
               changed the scene by hand (key or remote) in the last
               hold_minutes, and no calibration grid is up. The scene it
               replaced, and that scene's style, are remembered. Settings
               in while_showing (Music's theme 8, album) are set, and put
               back when it hands back.
  hand back    paused or stopped for idle_minutes (or on a release
               source, such as TV, for release_seconds): the scene and
               style it replaced come back, labelled "auto", so the
               playlist carries on. Only if the dome is still showing
               what this tool put there.
  stand down   if you change the scene while it has the dome, it lets
               go and does not take over again until the music has been
               quiet for idle_minutes (a new listening session).

The dome is reached through an object with state(), catalog(),
scene(n, by) and sset(scene, name, value) (domectl.Dome, or a fake in
the tests); the time is passed in, so the tests need not wait.
"""
import json
import pathlib

SHOWS = {"album", "radio", "record", "disc", "default"}
BEHAVIOURS = SHOWS | {"release", "off"}
HAND = {"key", "remote"}
BY = "nowplaying"

DEFAULT_CONFIG = pathlib.Path(__file__).resolve().parent / "sources.json"


def load_config(path=None):
    cfg = json.loads(pathlib.Path(path or DEFAULT_CONFIG).read_text())
    for r in cfg.get("rules", []):
        if r.get("behaviour") not in BEHAVIOURS:
            raise ValueError(f"unknown behaviour {r.get('behaviour')!r} in {path}")
    if cfg.get("unknown", "default") not in BEHAVIOURS:
        raise ValueError("unknown behaviour for 'unknown'")
    return cfg


def classify(media, cfg):
    """(behaviour, source name, audio_means_playing) for now-playing media."""
    media = media or {}
    try:
        sid = int(media.get("sid"))
    except (TypeError, ValueError):
        sid = None
    mid = str(media.get("mid") or "").lower()
    typ = str(media.get("type") or "").lower()
    for r in cfg.get("rules", []):
        if "sid" in r and r["sid"] != sid:
            continue
        if "mid_prefix" in r and not mid.startswith(r["mid_prefix"].lower()):
            continue
        if "type" in r and r["type"] != typ:
            continue
        return r["behaviour"], r.get("name", ""), bool(r.get("audio_means_playing"))
    return cfg.get("unknown", "default"), "unknown", False


class Switcher:
    def __init__(self, dome, cfg, log=print):
        self.dome = dome
        self.auto = bool(cfg.get("auto", True))
        self.hold = float(cfg.get("hold_minutes", 10)) * 60
        self.idle = float(cfg.get("idle_minutes", 5)) * 60
        self.release = float(cfg.get("release_seconds", 10))
        self.settle = float(cfg.get("settle_seconds", 3))
        self.names = [n.upper() for n in cfg.get("scenes", ["NOW PLAYING", "MUSIC"])]
        # settings to use while the music has the dome, per scene name,
        # put back as they were when it hands back
        self.while_showing = {k.upper(): v for k, v in cfg.get("while_showing", {}).items()}
        self.log = log
        self.taken = None            # what we replaced, while we hold the dome
        self.play_since = None
        self.quiet_since = None
        self.stood_down = False
        self.next_check = 0.0
        self.said = ""

    def _say(self, text):
        if text != self.said:
            self.said = text
            self.log(text)

    def update(self, now, playing, behaviour):
        """Call on every change and about once a second."""
        if not self.auto:
            return
        show = playing and behaviour in SHOWS
        if show:
            self.quiet_since = None
            if self.play_since is None:
                self.play_since = now
            if self.taken:
                if self.taken.get("todo"):
                    self._apply_settings()
                elif now >= self.next_check:
                    self.next_check = now + 5.0
                    self._still_ours()
            elif (not self.stood_down and now - self.play_since >= self.settle
                  and now >= self.next_check):
                self.next_check = now + 5.0      # not every second while waiting
                self._take(now)
            return
        self.play_since = None
        if self.quiet_since is None:
            self.quiet_since = now
        quiet = now - self.quiet_since
        if quiet >= self.idle:
            self.stood_down = False          # a new session may take over again
        if self.taken and behaviour != "off":
            wait = self.release if behaviour == "release" else self.idle
            if quiet >= wait:
                self._give_back()

    def _still_ours(self):
        st = self.dome.state()
        if st is None:
            return
        if st.get("by") != BY or st.get("scene") != self.taken["target"]:
            self._say(f"Scene changed by {st.get('by')}: leaving the dome alone "
                      "until the music stops for a while")
            self.taken = None
            self.stood_down = True

    def _target(self, st):
        scenes = [str(s).upper() for s in st.get("scenes", [])]
        for name in self.names:
            if name in scenes:
                return scenes.index(name)
        return None

    def _take(self, now):
        st = self.dome.state()
        if st is None:
            self._say("Hemisphere is not answering; will try again")
            return
        scene = st.get("scene", 0)
        if scene < 0:
            self._say("A calibration grid is up: not switching")
            return
        by, since = st.get("by"), float(st.get("since", 1e9))
        if by in HAND and since < self.hold:
            self._say(f"Scene was picked by hand {since / 60:.0f} min ago: not switching "
                      f"for another {(self.hold - since) / 60:.0f} min")
            return
        target = self._target(st)
        if target is None:
            self._say("No Now playing or Music scene in this Hemisphere")
            return
        if target == scene and by == BY:     # ours from before a restart
            self.taken = {"scene": None, "by": None, "pick": None, "target": target,
                          "todo": {}, "saved": {}}
            return
        if target == scene:
            self._say("Already showing the music scene")
            self.stood_down = True           # leave it be: it was not ours
            return
        pick = None
        cat = self.dome.catalog()
        try:
            sc = cat["scenes"][scene]
            if sc.get("pick"):
                pick = (sc["pick"], sc["value"])
        except (TypeError, KeyError, IndexError):
            pass
        if self.dome.scene(target, BY) is None:
            return
        self.taken = {"scene": scene, "by": by, "pick": pick, "target": target,
                      "todo": dict(self.while_showing.get(st["scenes"][target].upper(), {})),
                      "saved": {}}
        self.next_check = now + 5.0
        self._say(f"Music playing: showing {st['scenes'][target]}")

    def _apply_settings(self):
        """Once the switch has happened (the scene's settings show in
        state), note the ones we change and set them."""
        st = self.dome.state()
        if st is None or st.get("scene") != self.taken["target"]:
            return
        have = {p.get("name"): p.get("value") for p in st.get("params", [])
                if p.get("group") == "scene"}
        for k, v in self.taken["todo"].items():
            if k in have:
                self.taken["saved"][k] = have[k]
                self.dome.sset(self.taken["target"], k, float(v))
        self.taken["todo"] = {}

    def _give_back(self):
        t, self.taken = self.taken, None
        st = self.dome.state()
        if st is None or st.get("by") != BY or st.get("scene") != t["target"]:
            self._say("The music stopped; the scene was changed meanwhile, so leaving it")
            return
        for k, v in t["saved"].items():
            self.dome.sset(t["target"], k, float(v))
        if t["scene"] is None:
            self._say("The music stopped (the scene before is not known, so it stays)")
            return
        if t["pick"]:
            self.dome.sset(t["scene"], t["pick"][0], t["pick"][1])
        self.dome.scene(t["scene"], "auto")
        self._say("The music stopped: back to the previous scene")
