"""The network guard blocks the outside world; the fake device answers."""
import json
import socket
import urllib.request

import pytest

from conftest import LiveNetworkCall
from fakeheos import ArtServer, FakeHeos


def test_guard_blocks_tcp_to_the_lan():
    with pytest.raises(LiveNetworkCall):
        socket.create_connection(("192.0.2.10", 1255), timeout=0.1)


def test_guard_blocks_raw_connect_and_udp():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    with pytest.raises(LiveNetworkCall):
        s.connect(("198.51.100.7", 1255))
    s.close()
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    with pytest.raises(LiveNetworkCall):
        u.sendto(b"M-SEARCH", ("239.255.255.250", 1900))
    u.close()


def test_guard_blocks_name_lookups_and_http():
    with pytest.raises(LiveNetworkCall):
        socket.getaddrinfo("example.com", 80)
    with pytest.raises((LiveNetworkCall, urllib.error.URLError)):
        urllib.request.urlopen("http://example.com/", timeout=0.5)


def _ask(port, command):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
        s.sendall((command + "\r\n").encode())
        buf = b""
        while b"\r\n" not in buf:
            buf += s.recv(4096)
        return json.loads(buf.split(b"\r\n")[0])


def test_fake_answers_and_refuses():
    heos = FakeHeos().start()
    try:
        r = _ask(heos.port, "heos://player/get_players")
        assert r["payload"][0]["pid"] == 1234567
        r = _ask(heos.port, "heos://player/set_play_state?pid=1234567&state=stop")
        assert r["heos"]["result"] == "fail"
        assert heos.refused == ["heos://player/set_play_state?pid=1234567&state=stop"]
    finally:
        heos.stop()


def test_art_server_makes_covers():
    art = ArtServer().start()
    try:
        with urllib.request.urlopen(art.url("/cover/7.png"), timeout=2) as r:
            assert r.headers["Content-Type"] == "image/png"
            assert r.read(8) == b"\x89PNG\r\n\x1a\n"
    finally:
        art.stop()
