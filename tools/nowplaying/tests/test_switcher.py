"""The source rules and the auto-switch, with a fake dome and clock."""
import pytest

import switcher
from switcher import Switcher, classify, load_config

SCENES = ["DEATH STAR", "PLANET", "HYPNOTIC", "MUSIC"]
MUSIC = 3


class FakeDome:
    def __init__(self, scene=0, by="start", since=99999.0):
        self.scene_n, self.by, self.since = scene, by, since
        self.picks = {0: ("style", 2.0), 1: None, 2: ("style", 5.0), 3: ("style", 0.0)}
        self.sent = []
        self.up = True

    def state(self):
        if not self.up:
            return None
        return {"scene": self.scene_n, "by": self.by, "since": self.since, "scenes": SCENES}

    def catalog(self):
        out = []
        for i, n in enumerate(SCENES):
            p = self.picks.get(i)
            out.append({"name": n, "pick": p[0], "value": p[1]} if p else {"name": n})
        return {"scenes": out}

    def scene(self, n, by):
        self.sent.append(f"scene {n} {by}")
        self.scene_n, self.by, self.since = n, by, 0.0
        return "ok"

    def sset(self, scene, name, value):
        self.sent.append(f"sset {scene} {name} {value:g}")
        return "ok"

    def by_hand(self, n, by="key"):
        self.scene_n, self.by, self.since = n, by, 0.0


@pytest.fixture
def cfg():
    return load_config()


def run(sw, dome, t0, t1, playing, behaviour, step=1.0):
    t = t0
    while t <= t1:
        sw.update(t, playing, behaviour)
        dome.since += step
        t += step
    return t


# ---- the source rules ---------------------------------------------
@pytest.mark.parametrize("media,want", [
    ({"sid": 1027, "mid": "inputs/phono", "type": "station"}, "record"),
    ({"sid": 1027, "mid": "inputs/coax_in_1", "type": "station"}, "disc"),
    ({"sid": 1027, "mid": "inputs/optical_in_1", "type": "station"}, "release"),
    ({"sid": 1027, "mid": "inputs/cd", "type": "station"}, "default"),
    ({"sid": 1027, "mid": "inputs/aux_in_1", "type": "station"}, "default"),
    ({"sid": 4, "mid": "spotify:track:x", "type": "song"}, "album"),
    ({"sid": 3, "mid": "s1234", "type": "station"}, "radio"),
    ({"sid": 999, "mid": "x", "type": "song"}, "album"),
    ({"sid": 999, "mid": "x", "type": "station"}, "radio"),
    ({}, "default"),
    (None, "default"),
    ({"sid": "junk"}, "default"),
])
def test_classify(cfg, media, want):
    assert classify(media, cfg)[0] == want


def test_phono_counts_sound_as_playing(cfg):
    assert classify({"sid": 1027, "mid": "inputs/phono"}, cfg)[2] is True
    assert classify({"sid": 4, "type": "song"}, cfg)[2] is False


def test_config_rejects_unknown_behaviour(tmp_path):
    p = tmp_path / "s.json"
    p.write_text('{"rules": [{"sid": 1, "behaviour": "dance"}]}')
    with pytest.raises(ValueError):
        load_config(p)


# ---- switching ------------------------------------------------------
def test_takes_over_after_settling_and_gives_back(cfg):
    dome = FakeDome(scene=2)
    sw = Switcher(dome, cfg, log=lambda s: None)
    sw.update(0, True, "album")
    sw.update(2, True, "album")
    assert dome.sent == []                                   # still settling
    sw.update(3, True, "album")
    assert dome.sent == [f"scene {MUSIC} nowplaying"]
    run(sw, dome, 4, 100, True, "album")                     # track changes etc.
    assert len(dome.sent) == 1
    run(sw, dome, 101, 101 + 299, False, "album")            # paused 5 min less a bit
    assert len(dome.sent) == 1
    run(sw, dome, 401, 402, False, "album")
    assert dome.sent[1:] == ["sset 2 style 5", "scene 2 auto"]


def test_resuming_cancels_the_idle_timer(cfg):
    dome = FakeDome(scene=0)
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 5, True, "album")
    run(sw, dome, 6, 200, False, "album")
    run(sw, dome, 201, 210, True, "album")
    run(sw, dome, 211, 400, False, "album")
    assert dome.sent == [f"scene {MUSIC} nowplaying"]


def test_release_source_gives_back_quickly(cfg):
    dome = FakeDome(scene=1)
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 5, True, "album")
    run(sw, dome, 6, 16, True, "release")              # 10 s after it switched
    assert dome.sent[-1] == "scene 1 auto"
    assert "sset" not in " ".join(dome.sent)                # Planet has no style


def test_off_source_never_takes_over(cfg):
    dome = FakeDome()
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 60, True, "off")
    run(sw, dome, 61, 60 * 60, True, "release")
    assert dome.sent == []


def test_respects_a_recent_hand_change(cfg):
    dome = FakeDome(scene=1, by="key", since=60.0)          # a minute ago
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 500, True, "album")                     # 9 min 20 s in all
    assert dome.sent == []
    run(sw, dome, 501, 560, True, "album")                   # past 10 min
    assert dome.sent == [f"scene {MUSIC} nowplaying"]


def test_remote_counts_as_by_hand_but_playlist_does_not(cfg):
    dome = FakeDome(scene=1, by="remote", since=10.0)
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 30, True, "album")
    assert dome.sent == []
    dome2 = FakeDome(scene=1, by="playlist", since=10.0)
    sw2 = Switcher(dome2, cfg, log=lambda s: None)
    run(sw2, dome2, 0, 30, True, "album")
    assert dome2.sent == [f"scene {MUSIC} nowplaying"]


def test_stands_down_when_you_change_scene(cfg):
    dome = FakeDome(scene=0)
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 5, True, "album")
    dome.by_hand(1)
    run(sw, dome, 6, 60 * 30, True, "album")                # 30 min more music
    assert dome.sent == [f"scene {MUSIC} nowplaying"]       # never took it back
    run(sw, dome, 60 * 30 + 1, 60 * 36, False, "album")     # quiet 5+ min: new session
    assert dome.scene_n == 1                                 # your scene stays
    dome.since = 99999
    run(sw, dome, 60 * 36 + 1, 60 * 36 + 10, True, "album")
    assert dome.sent[-1] == f"scene {MUSIC} nowplaying"


def test_does_not_give_back_if_you_moved_away_while_paused(cfg):
    dome = FakeDome(scene=0)
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 5, True, "album")
    sw.update(6, False, "album")
    dome.by_hand(2)
    run(sw, dome, 7, 400, False, "album")
    assert dome.sent == [f"scene {MUSIC} nowplaying"]
    assert dome.scene_n == 2


def test_leaves_calibration_grids_alone(cfg):
    dome = FakeDome(scene=-2)
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 60, True, "album")
    assert dome.sent == []


def test_already_on_music_by_hand(cfg):
    dome = FakeDome(scene=MUSIC, by="playlist")
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 30, True, "album")
    run(sw, dome, 31, 1000, False, "album")
    assert dome.sent == []


def test_hemisphere_not_running_then_starting(cfg):
    dome = FakeDome(scene=0)
    dome.up = False
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 20, True, "album")
    assert dome.sent == []
    dome.up = True
    run(sw, dome, 21, 30, True, "album")
    assert dome.sent == [f"scene {MUSIC} nowplaying"]


def test_turned_off(cfg):
    cfg = dict(cfg, auto=False)
    dome = FakeDome()
    sw = Switcher(dome, cfg, log=lambda s: None)
    run(sw, dome, 0, 60, True, "album")
    assert dome.sent == []


def test_prefers_now_playing_scene_when_present(cfg):
    dome = FakeDome()
    old = SCENES[:]
    SCENES.append("NOW PLAYING")
    try:
        sw = Switcher(dome, cfg, log=lambda s: None)
        run(sw, dome, 0, 5, True, "album")
        assert dome.sent == ["scene 4 nowplaying"]
    finally:
        SCENES[:] = old


def test_shows_set():
    assert switcher.SHOWS == {"album", "radio", "record", "disc", "default"}
