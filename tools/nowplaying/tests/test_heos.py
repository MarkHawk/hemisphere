"""The read-only HEOS client, against fixtures and the fake device."""
import json
import socket
import threading

import pytest

import heos
from fakeheos import PID, FakeHeos


# ---- the allow-list ------------------------------------------------
@pytest.mark.parametrize("command,params", [
    ("player/set_play_state", {"pid": PID, "state": "pause"}),
    ("player/set_volume", {"pid": PID, "level": 10}),
    ("player/volume_up", {"pid": PID}),
    ("player/play_next", {"pid": PID}),
    ("player/set_mute", {"pid": PID, "state": "on"}),
    ("browse/play_input", {"pid": PID, "input": "inputs/phono"}),
    ("browse/play_stream", {"pid": PID, "sid": 3, "mid": "s1"}),
    ("system/reboot", {}),
    ("system/sign_in", {"un": "x", "pw": "y"}),
])
def test_refuses_anything_that_changes_the_amp(command, params):
    with pytest.raises(heos.NotAllowed):
        heos.build(command, **params)


def test_refuses_extra_or_odd_values():
    with pytest.raises(heos.NotAllowed):
        heos.build("system/register_for_change_events", enable="maybe")
    with pytest.raises(heos.NotAllowed):
        heos.build("player/get_play_state", pid=PID, state="play")


def test_builds_allowed_lines():
    assert heos.build("player/get_players") == "heos://player/get_players\r\n"
    assert heos.build("player/get_play_state", pid=PID) == \
        f"heos://player/get_play_state?pid={PID}\r\n"


# ---- parsing --------------------------------------------------------
def test_parse_message_decodes_heos_escapes():
    m = heos.parse_message("pid=1&song=Rock%20%26%20Roll%3D1%25&command under process")
    assert m == {"pid": "1", "song": "Rock & Roll=1%", "command under process": ""}


def test_parse_fixture_lines(fixtures):
    lines = (fixtures / "sample_session.jsonl").read_text().splitlines()
    msgs = [heos.parse_line(l) for l in lines]
    assert msgs[1]["payload"][0]["pid"] == PID
    assert msgs[2]["message"]["state"] == "play"
    assert msgs[3]["payload"]["sid"] == 4
    assert msgs[4]["is_event"] and msgs[4]["message"]["cur_pos"] == "61000"
    assert msgs[7]["payload"]["mid"] == "inputs/phono"
    assert "command under process" in msgs[10]["message"]
    assert msgs[11]["result"] == "fail"


def test_split_host():
    assert heos.split_host("amp") == ("amp", 1255)
    assert heos.split_host("127.0.0.1:11255") == ("127.0.0.1", 11255)
    assert heos.split_host("[::1]:9") == ("::1", 9)


def test_choose_player():
    ps = [{"name": "Kitchen", "pid": 1, "model": "A"}, {"name": "Lounge", "pid": 2, "model": "B"}]
    assert heos.choose_player(ps)["pid"] == 1
    assert heos.choose_player(ps, "lounge")["pid"] == 2
    assert heos.choose_player(ps, "2")["name"] == "Lounge"
    assert heos.choose_player(ps, "b")["pid"] == 2
    assert heos.choose_player(ps, "garage") is None
    assert heos.choose_player([]) is None


# ---- against the fake device ----------------------------------------
@pytest.fixture
def fake():
    f = FakeHeos().start()
    yield f
    f.stop()


def test_session_with_fake(fake):
    fake.play({"type": "song", "song": "A & B", "artist": "C", "album": "D",
               "image_url": "", "mid": "m", "sid": 4})
    c = heos.Client("127.0.0.1", fake.port)
    try:
        assert c.players()[0]["name"] == "Test Amp"
        assert c.now_playing(PID)["song"] == "A & B"
        assert c.play_state(PID) == "play"
        fake.set_state("pause")
        e = c.next_event(2.0)
        assert e == {"event": "player_state_changed", "pid": str(PID), "state": "pause"}
        fake.progress(5000, 200000)
        e = c.next_event(2.0)
        assert e["event"] == "player_now_playing_progress" and e["duration"] == "200000"
        assert c.next_event(0.1) is None
        c.last_beat = 0
        c.heartbeat()
    finally:
        c.close()
    # nothing outside the read-only list ever reached the device
    assert fake.refused == []
    assert all(cmd.split("?")[0][7:] in heos.ALLOWED for cmd in fake.commands)


def test_disconnect_is_reported(fake):
    c = heos.Client("127.0.0.1", fake.port)
    fake.drop_clients()
    with pytest.raises(heos.Disconnected):
        for _ in range(20):
            c.next_event(0.2)
    c.close()


def _scripted_server(lines):
    """A server that answers the first command with the given lines."""
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)

    def run():
        conn, _ = srv.accept()
        conn.recv(4096)
        for l in lines:
            conn.sendall((json.dumps(l) + "\r\n").encode())
        conn.recv(4096)
        conn.close()
        srv.close()
    threading.Thread(target=run, daemon=True).start()
    return srv.getsockname()[1]


def test_request_waits_past_events_and_under_process():
    port = _scripted_server([
        {"heos": {"command": "event/player_state_changed", "message": "pid=1&state=play"}},
        {"heos": {"command": "player/get_players", "result": "success",
                  "message": "command under process"}},
        {"heos": {"command": "player/get_players", "result": "success", "message": ""},
         "payload": [{"pid": 1}]},
    ])
    c = heos.Connection("127.0.0.1", port)
    r = c.request("player/get_players")
    assert r["payload"] == [{"pid": 1}]
    assert c.next_event(0.1)["message"]["state"] == "play"
    c.close()


def test_request_failure_raises():
    port = _scripted_server([
        {"heos": {"command": "player/get_play_state", "result": "fail",
                  "message": "eid=2&text=ID Not Valid"}}])
    c = heos.Connection("127.0.0.1", port)
    with pytest.raises(heos.HeosError, match="ID Not Valid"):
        c.request("player/get_play_state", pid=9)
    c.close()


def test_discover_on_loopback():
    resp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    resp.bind(("127.0.0.1", 0))

    def answer():
        data, addr = resp.recvfrom(4096)
        assert b"M-SEARCH" in data
        resp.sendto(b"HTTP/1.1 200 OK\r\nST: " + heos.SSDP_ST.encode() + b"\r\n\r\n", addr)
    threading.Thread(target=answer, daemon=True).start()
    assert heos.discover(1.0, target=resp.getsockname()) == ["127.0.0.1"]
    resp.close()


def test_recorded_amp_phono_stopped(fixtures):
    """A real amp's answers (anonymised): phono selected, nothing playing."""
    import switcher
    rows = [json.loads(l) for l in (fixtures / "recorded_phono_stopped.jsonl").read_text().splitlines()]
    sent = [r["line"] for r in rows if r["direction"] == "sent"]
    assert all(s.split("?")[0][7:] in heos.ALLOWED for s in sent)       # read only
    got = [r["line"] for r in rows if r["direction"] == "received"]
    media = next(g["payload"] for g in got if g["heos"]["command"] == "player/get_now_playing_media")
    state = next(heos.parse_message(g["heos"]["message"])["state"] for g in got
                 if g["heos"]["command"] == "player/get_play_state")
    assert state == "stop"
    assert switcher.classify(media, switcher.load_config())[:3] == ("record", "phono", True)
