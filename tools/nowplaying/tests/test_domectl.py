"""The dome link, against a fake control channel on loopback."""
import socket
import threading

import pytest

import domectl


@pytest.fixture
def channel():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    got = []

    def run():
        while True:
            try:
                data, addr = s.recvfrom(4096)
            except OSError:
                return
            got.append(data.decode())
            s.sendto(b'{"scene":1}' if data == b"state" else b"ok", addr)
    threading.Thread(target=run, daemon=True).start()
    yield s.getsockname()[1], got
    s.close()


def test_commands(channel, tmp_path):
    port, got = channel
    d = domectl.Dome(port=port, data_dir=tmp_path)
    assert d.state() == {"scene": 1}
    d.palette(["ff0000", "00ff00", "0000ff", "111111"])
    d.palette(None)
    d.progress(1500.7, 200000, "play")
    d.progress(-5, 0, "dance")
    d.scene(3, "nowplaying")
    d.sset(2, "style", 5.0)
    assert got == ["state", "palette ff0000 00ff00 0000ff 111111", "palette off",
                   "progress 1500 200000 play", "progress 0 0 stop",
                   "scene 3 nowplaying", "sset 2 style 5"]


def test_publish_writes_ascii_now_txt(channel, tmp_path):
    port, got = channel
    d = domectl.Dome(port=port, data_dir=tmp_path)
    serial = d.publish({"title": "Déjà Vu\n(Live)", "artist": "Beyoncé", "album": "東京",
                        "source": "Spotify", "behaviour": "album", "art": True})
    text = (tmp_path / "now.txt").read_text()
    assert f"serial={serial}" in text
    assert "title=Deja Vu (Live)" in text and "artist=Beyonce" in text
    assert "album=\n" in text and "art=1" in text
    assert got[-1] == f"np {serial}"
    assert d.publish({}) == serial + 1


def test_not_running(tmp_path):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    d = domectl.Dome(port=port, data_dir=tmp_path, timeout=0.2)
    assert d.state() is None and d.scene(1, "x") is None
