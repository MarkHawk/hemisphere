"""The whole tool: fake amp, fake covers, fake hemisphere, all on loopback."""
import json
import socket
import threading
import time

import pytest

import heos
import nowplaying
from domectl import Dome
from fakeheos import PID, ArtServer, FakeHeos
from switcher import load_config

SCENES = ["DEATH STAR", "PLANET", "MUSIC"]


class FakeHemisphere:
    """Answers the control channel like the real program."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.scene, self.by, self.t = 1, "start", time.monotonic() - 9999
        self.serial, self.live = 0, 0.0
        self.got = []
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(4096)
            except OSError:
                return
            c = data.decode()
            self.got.append(c)
            reply = "ok"
            if c == "state":
                reply = json.dumps({"scene": self.scene, "by": self.by,
                                    "since": time.monotonic() - self.t, "scenes": SCENES,
                                    "audio": {"live": self.live},
                                    "nowplaying": {"serial": self.serial}})
            elif c == "catalog":
                reply = json.dumps({"scenes": [{"name": n} for n in SCENES]})
            elif c.startswith("scene "):
                _, n, by = c.split()
                self.scene, self.by, self.t = int(n), by, time.monotonic()
            elif c.startswith("np "):
                self.serial = int(c.split()[1])
            self.sock.sendto(reply.encode(), addr)

    def close(self):
        self.sock.close()

    def wait_for(self, pred, timeout=5.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if pred():
                return True
            time.sleep(0.05)
        return False


@pytest.fixture
def rig(tmp_path):
    amp = FakeHeos().start()
    covers = ArtServer().start()
    hemi = FakeHemisphere()
    cfg = dict(load_config(), settle_seconds=0.3, idle_minutes=1.5 / 60,
               release_seconds=0.5)
    dome = Dome(port=hemi.port, data_dir=tmp_path, timeout=0.5)
    logs = []
    np = nowplaying.NowPlaying(lambda: heos.Client("127.0.0.1", amp.port), dome, cfg,
                               data_dir=tmp_path, log=logs.append)
    stop = threading.Event()
    yield amp, covers, hemi, np, stop, tmp_path, logs
    stop.set()
    time.sleep(0.7)
    amp.stop()
    covers.stop()
    hemi.close()


def song(covers, n):
    return {"type": "station", "song": f"Track {n}", "station": f"Track {n}",  # as Spotify does
            "artist": "The Testers", "album": "Fixtures",
            "image_url": covers.url(f"/cover/{n}.png"), "mid": f"m{n}", "sid": 4}


def test_full_cycle(rig):
    amp, covers, hemi, np, stop, data, logs = rig
    amp.play(song(covers, 1))
    threading.Thread(target=np.run, args=(stop,), daemon=True).start()

    # the cover, palette and details arrive, then the dome switches
    assert hemi.wait_for(lambda: any(c.startswith("np ") for c in hemi.got))
    assert len((data / "art.rgba").read_bytes()) == 512 * 512 * 4
    now = (data / "now.txt").read_text()
    assert "title=Track 1" in now and "art=1" in now and "behaviour=album" in now
    assert "station=\n" in now                    # not the song title again
    assert any(c.startswith("palette ") and c != "palette off" for c in hemi.got)
    assert hemi.wait_for(lambda: hemi.scene == 2 and hemi.by == "nowplaying")

    # progress is passed on
    amp.progress(12000, 180000)
    assert hemi.wait_for(lambda: "progress 12000 180000 play" in hemi.got)

    # a new track: new details, no scene change
    amp.play(song(covers, 2))
    assert hemi.wait_for(lambda: "title=Track 2" in (data / "now.txt").read_text())
    assert [c for c in hemi.got if c.startswith("scene ")] == ["scene 2 nowplaying"]

    # paused past the idle time: the previous scene comes back
    amp.set_state("pause")
    assert hemi.wait_for(lambda: hemi.scene == 1 and hemi.by == "auto", timeout=6)

    # nothing that changes the amp was ever sent
    assert amp.refused == []
    assert all(c.split("?")[0][7:] in heos.ALLOWED for c in amp.commands)


def test_cd_keeps_colours_optical_releases_phono_shows_without_art(rig):
    amp, covers, hemi, np, stop, data, logs = rig
    amp.play(song(covers, 3))
    threading.Thread(target=np.run, args=(stop,), daemon=True).start()
    assert hemi.wait_for(lambda: hemi.scene == 2)
    pal = [c for c in hemi.got if c.startswith("palette ")][-1]
    amp.play({"type": "station", "song": "Test Amp - Coaxial", "station": "Test Amp - Coaxial",
              "image_url": "", "mid": "inputs/coax_in_1", "sid": 1027})
    assert hemi.wait_for(lambda: "behaviour=disc" in (data / "now.txt").read_text())
    assert [c for c in hemi.got if c.startswith("palette ")][-1] == pal   # colours kept
    assert hemi.scene == 2                                                # still shown
    amp.play({"type": "station", "song": "Test Amp - Optical", "station": "Test Amp - Optical",
              "image_url": "", "mid": "inputs/optical_in_1", "sid": 1027})
    assert hemi.wait_for(lambda: hemi.scene == 1 and hemi.by == "auto", timeout=4)
    amp.play({"type": "station", "song": "Test Amp - Phono", "station": "Test Amp - Phono",
              "image_url": "", "mid": "inputs/phono", "sid": 1027})
    assert hemi.wait_for(lambda: "title=Phono" in (data / "now.txt").read_text())
    now = (data / "now.txt").read_text()
    assert "art=0" in now and "behaviour=record" in now and "station=Phono" in now
    assert "palette off" in hemi.got


def test_phono_plays_by_sound_when_the_amp_says_stop(rig):
    amp, covers, hemi, np, stop, data, logs = rig
    amp.play({"type": "station", "song": "Test Amp - Phono", "station": "Test Amp - Phono",
              "image_url": "", "mid": "inputs/phono", "sid": 1027}, state="stop")
    threading.Thread(target=np.run, args=(stop,), daemon=True).start()
    time.sleep(1.5)
    assert hemi.scene == 1                       # silence: stays put
    hemi.live = 1.0                              # the record starts
    assert hemi.wait_for(lambda: hemi.scene == 2 and hemi.by == "nowplaying", timeout=8)


def test_amp_going_away_and_hemisphere_restarting(rig):
    amp, covers, hemi, np, stop, data, logs = rig
    amp.play(song(covers, 4))
    threading.Thread(target=np.run, args=(stop,), daemon=True).start()
    assert hemi.wait_for(lambda: hemi.scene == 2)
    first = hemi.serial
    hemi.serial = 0                              # as if hemisphere restarted
    assert hemi.wait_for(lambda: hemi.serial > first, timeout=12)
    amp.drop_clients()                           # standby: connection lost
    assert hemi.wait_for(lambda: any("Amp:" in l for l in logs), timeout=4)
    assert hemi.wait_for(lambda: hemi.scene == 1 and hemi.by == "auto", timeout=6)


def test_dry_run_sends_nothing(tmp_path, capsys):
    d = Dome(data_dir=tmp_path, dry_run=True)
    assert d.scene(1, "nowplaying") == "ok"
    assert "[dry run] scene 1 nowplaying" in capsys.readouterr().out


def test_scrubber_and_scan(tmp_path):
    sc = nowplaying.Scrubber()
    sc.learn([{"name": "Front Room", "pid": 987654321, "model": "Brand X9000", "ip": "10.1.2.3",
               "serial": "ABC123456", "network": "wifi"}], "10.1.2.3")
    raw = {"heos": {"command": "player/get_players", "message": "pid=987654321"},
           "payload": [{"name": "Front Room", "pid": 987654321, "model": "Brand X9000",
                        "ip": "10.1.2.3", "serial": "ABC123456", "network": "wifi",
                        "un": "me@example.com"}],
           "extra": {"song": "Front Room - Phono", "url": "http://10.9.9.9/a.jpg",
                     "note": "X9000 at 10.1.2.3, mac aa:bb:cc:dd:ee:ff"}}
    out = sc.obj(raw)
    p = tmp_path / "f.jsonl"
    p.write_text(json.dumps(out))
    assert out["extra"]["song"] == "Test Amp - Phono"
    assert out["payload"][0]["un"] == "<account>"
    assert all(not found for _, found in sc.scan(p)), sc.scan(p)
    p.write_text(json.dumps(raw))
    assert any(found for _, found in sc.scan(p))


def test_record_writes_scrubbed_fixture(tmp_path):
    amp = FakeHeos().start()
    try:
        amp.play({"type": "song", "song": "Test Amp", "sid": 4})
        path, clean = nowplaying.record(f"127.0.0.1:{amp.port}", None, tmp_path, 1,
                                        log=lambda s: None)
    finally:
        amp.stop()
    assert clean
    body = path.read_text()
    assert '"pid": 1234567' in body or "pid=1234567" in body
    assert amp.refused == []
