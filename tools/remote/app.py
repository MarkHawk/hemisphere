#!/usr/bin/env python3
r"""Hemisphere web remote.

A small Flask app that serves a phone-friendly remote control page and a
JSON API, and passes commands to the running hemisphere program over its
control channel (UDP on 127.0.0.1:5005).

    venv/bin/python3 tools/remote/app.py        then open http://<pi>:5000
    venv\Scripts\python tools\remote\app.py    (Windows)

API
    GET  /api/state                  current scene, its settings, fps, feed age
    POST /api/scene    {"scene": 2}  switch scene (0-based; -2 lens grid, -1 globe grid)
    POST /api/key      {"key": "n"}  press a key: n, m, c, s, space, up, down, 1-9, 0, "," "."
    POST /api/setting  {"name": "speed", "value": 2.5}
    POST /api/save                   save all settings files (like pressing S)
    GET  /api/catalog                every scene and the setting that picks its style
    GET  /api/playlist               the playlist and what it's doing now
    POST /api/playlist  {...}        replace the playlist (see playlist.py)
    POST /api/playlist/skip {"step": 1}   next (1) or previous (-1) now

Anyone on your home network can open the page. There is no login, so keep
port 5000 off the internet.
"""
import json
import os
import pathlib
import socket

from flask import Flask, jsonify, request, send_from_directory

from playlist import Playlist

HERE = pathlib.Path(__file__).resolve().parent
CTL = ("127.0.0.1", int(os.environ.get("DOME_CTL_PORT", "5005")))
KEYS = set("abcdefghijklmnopqrstuvwxyz0123456789,.") | {"space", "up", "down", "left", "right", "tab"}

app = Flask(__name__, static_folder=str(HERE / "static"), static_url_path="/static")


def send(command, timeout=1.0):
    """Send one command to hemisphere and return its reply, or None."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(timeout)
        try:
            s.sendto(command.encode(), CTL)
            return s.recvfrom(65535)[0].decode("utf-8", "replace")
        except OSError:
            return None


PLAYLIST_FILE = os.environ.get("HEMISPHERE_PLAYLIST", str(HERE.parent.parent / "playlist.json"))
playlist = Playlist(send, PLAYLIST_FILE)


def result(reply):
    if reply is None:
        return jsonify(ok=False, error="hemisphere is not running"), 503
    if reply.startswith("error"):
        return jsonify(ok=False, error=reply[6:].strip()), 400
    return jsonify(ok=True)


@app.get("/")
def index():
    return send_from_directory(HERE / "static", "index.html")


@app.get("/api/state")
def state():
    reply = send("state")
    if reply is None:
        return jsonify(online=False), 503
    data = json.loads(reply)
    data["online"] = True
    return jsonify(data)


@app.post("/api/scene")
def scene():
    body = request.get_json(silent=True) or {}
    try:
        n = int(body["scene"])
    except (KeyError, TypeError, ValueError):
        return jsonify(ok=False, error="scene must be a number"), 400
    playlist.touch()
    return result(send(f"scene {n}"))


@app.post("/api/key")
def key():
    body = request.get_json(silent=True) or {}
    k = str(body.get("key", "")).lower()
    if k not in KEYS:
        return jsonify(ok=False, error="unknown key"), 400
    playlist.touch()
    return result(send(f"key {k}"))


@app.post("/api/setting")
def setting():
    body = request.get_json(silent=True) or {}
    name = str(body.get("name", ""))
    try:
        value = float(body["value"])
    except (KeyError, TypeError, ValueError):
        return jsonify(ok=False, error="value must be a number"), 400
    if not name or not all(c.isalnum() or c == "_" for c in name):
        return jsonify(ok=False, error="bad setting name"), 400
    playlist.touch()
    return result(send(f"set {name} {value:g}"))


@app.post("/api/save")
def save():
    return result(send("save"))


@app.get("/api/catalog")
def catalog():
    cat = playlist.get_catalog()
    if cat is None:
        return jsonify(ok=False, error="hemisphere is not running"), 503
    return jsonify(ok=True, scenes=cat)


@app.get("/api/playlist")
def playlist_get():
    return jsonify(ok=True, playlist=playlist.cfg, status=playlist.status())


@app.post("/api/playlist")
def playlist_set():
    body = request.get_json(silent=True)
    if not isinstance(body, dict):
        return jsonify(ok=False, error="expected a playlist"), 400
    try:
        playlist.update(body)
    except OSError as e:
        return jsonify(ok=False, error=f"couldn't save the playlist: {e}"), 500
    return jsonify(ok=True, playlist=playlist.cfg, status=playlist.status())


@app.post("/api/playlist/skip")
def playlist_skip():
    body = request.get_json(silent=True) or {}
    playlist.skip(-1 if body.get("step") == -1 else 1)
    return jsonify(ok=True, status=playlist.status())


if __name__ == "__main__":
    app.run(host=os.environ.get("REMOTE_HOST", "0.0.0.0"),
            port=int(os.environ.get("REMOTE_PORT", "5000")))
