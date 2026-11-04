#!/usr/bin/env python3
"""Measure the frame rate of every scene and style, over the control channel.

Run it on the Pi itself (the control channel only listens on 127.0.0.1),
with Hemisphere running and nothing else switching scenes: stop the web
remote (its playlist) and the now-playing tool first.

    venv/bin/python tools/fpssweep.py --out fps.csv
    venv/bin/python tools/fpssweep.py --scenes 0,17 --vary 0:audio=0,1

For each scene and each value of its pick setting (style, theme or view,
from the catalog), it sets the value, switches to the scene, checks with
"state" that both landed, waits for the frame rate to settle, then reads
"state" every couple of seconds. Hemisphere counts frames over 2 second
windows, so min and max are per window, not per frame. A row is marked
"moved" if the scene changed under it while measuring.

Every pick, any --vary setting and the scene on screen are put back at
the end. Settings are never saved. Each row also records the CPU
temperature and vcgencmd get_throttled, so a throttle mid-run shows which
rows it touched. Standard library only.
"""
import argparse
import csv
import json
import re
import socket
import subprocess
import sys
import time

FIELDS = ["scene", "scene_name", "pick", "value", "value_name", "variant",
          "samples", "min", "avg", "max", "temp_c", "throttled", "status"]


class Ctl:
    """The control channel. Replies carry no tag, so a reply that comes
    after its timeout (a scene rebuilding its textures can hold the main
    loop for a second) would be read as the answer to the next command:
    anything left waiting is thrown away before each send."""

    def __init__(self, port: int, timeout: float = 3.0):
        self.addr = ("127.0.0.1", port)
        self.timeout = timeout
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(timeout)

    def _drain(self) -> None:
        self.s.setblocking(False)
        try:
            while True:
                self.s.recv(65536)
        except BlockingIOError:
            pass
        finally:
            self.s.settimeout(self.timeout)

    def ask(self, cmd: str) -> str:
        for _ in range(3):
            self._drain()
            self.s.sendto(cmd.encode(), self.addr)
            try:
                return self.s.recv(65536).decode()
            except socket.timeout:
                continue
        raise RuntimeError(f"no reply to {cmd!r} on port {self.addr[1]}")

    def json(self, cmd: str) -> dict:
        for _ in range(3):
            reply = self.ask(cmd)
            try:
                return json.loads(reply)
            except ValueError:
                continue
        raise RuntimeError(f"no JSON reply to {cmd!r}: {reply[:60]!r}")


def choice_name(hint: str, n: int) -> str:
    """Choice n in a hint such as "look: 0 wireframe, 1 hull (N)"."""
    hint = re.sub(r"\([A-Z]( cycles)?\)\s*$", "", hint)
    hint = re.sub(r"^[a-z ]+:\s*", "", hint)
    for part in hint.split(","):
        m = re.match(r"\s*(\d+)\s*=?\s*(.*)", part)
        if m and int(m.group(1)) == n:
            return m.group(2).strip()
    return ""


def param(state: dict, name: str):
    for p in state.get("params", []):
        if p["name"] == name:
            return p["value"]
    return None


def temp_c() -> str:
    try:
        out = subprocess.run(["vcgencmd", "measure_temp"], capture_output=True,
                             text=True, timeout=2).stdout
        return re.search(r"([\d.]+)", out).group(1)
    except Exception:
        return ""


def throttled() -> str:
    """vcgencmd get_throttled, such as 0x0 (nothing since boot)."""
    try:
        out = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True,
                             text=True, timeout=2).stdout
        return out.strip().split("=")[-1]
    except Exception:
        return ""


def parse_vary(specs: list[str]) -> dict[int, tuple[str, list[float]]]:
    out = {}
    for spec in specs:
        m = re.fullmatch(r"(\d+):(\w+)=([\d.,-]+)", spec)
        if not m:
            sys.exit(f"--vary {spec!r}: expected SCENE:SETTING=V1,V2")
        out[int(m.group(1))] = (m.group(2), [float(v) for v in m.group(3).split(",")])
    return out


def show(ctl: Ctl, n: int, sets: list[tuple[str, float]], tries: int = 3) -> bool:
    """Apply settings to scene n, switch to it and check it all landed."""
    for _ in range(tries):
        for name, v in sets:
            ctl.ask(f"sset {n} {name} {v:g}")
        ctl.ask(f"scene {n} fpssweep")
        time.sleep(0.5)
        st = ctl.json("state")
        if st.get("scene") == n and all(param(st, k) == v for k, v in sets):
            return True
    return False


def measure(ctl: Ctl, n: int, seconds: float, every: float):
    fps, moved = [], False
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        time.sleep(every)
        st = ctl.json("state")
        if st.get("scene") != n:
            moved = True
        fps.append(float(st["fps"]))
    return fps, moved


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=5005, help="control channel port (default 5005)")
    ap.add_argument("--out", default="-", help="CSV file to write (default: standard output)")
    ap.add_argument("--scenes", help="only these scene numbers, comma separated")
    ap.add_argument("--vary", action="append", default=[], metavar="SCENE:SETTING=V1,V2",
                    help="also sweep each style of SCENE under each value of SETTING")
    ap.add_argument("--settle", type=float, default=5.0, help="seconds before measuring (default 5)")
    ap.add_argument("--measure", type=float, default=20.0, help="seconds to measure (default 20)")
    ap.add_argument("--every", type=float, default=2.0,
                    help="seconds between readings (default 2, Hemisphere's window)")
    args = ap.parse_args()

    ctl = Ctl(args.port)
    cat = ctl.json("catalog")["scenes"]
    start = ctl.json("state")
    only = {int(x) for x in args.scenes.split(",")} if args.scenes else None
    vary = parse_vary(args.vary)

    out = sys.stdout if args.out == "-" else open(args.out, "w", newline="")
    w = csv.DictWriter(out, fieldnames=FIELDS)
    w.writeheader()
    restore: list[tuple[int, str, float]] = []
    try:
        for n, sc in enumerate(cat):
            if only is not None and n not in only:
                continue
            pick = sc.get("pick")
            if pick:
                restore.append((n, pick, sc["value"]))
                values = range(int(sc["lo"]), int(sc["hi"]) + 1)
            else:
                values = [None]
            variants: list[tuple[str, float] | None] = [None]
            if n in vary:
                name, vs = vary[n]
                if show(ctl, n, []):
                    orig = param(ctl.json("state"), name)
                    if orig is not None:
                        restore.append((n, name, orig))
                variants = [(name, v) for v in vs]
            for var in variants:
                for v in values:
                    sets = ([(pick, float(v))] if v is not None else []) + ([var] if var else [])
                    row = {"scene": n, "scene_name": sc["name"], "pick": pick or "",
                           "value": "" if v is None else v,
                           "value_name": choice_name(sc.get("hint", ""), v) if v is not None else "",
                           "variant": f"{var[0]}={var[1]:g}" if var else ""}
                    if not show(ctl, n, sets):
                        row.update(samples=0, status="FAILED", temp_c=temp_c(), throttled=throttled())
                    else:
                        time.sleep(args.settle)
                        fps, moved = measure(ctl, n, args.measure, args.every)
                        row.update(samples=len(fps), min=f"{min(fps):.1f}",
                                   avg=f"{sum(fps) / len(fps):.1f}", max=f"{max(fps):.1f}",
                                   temp_c=temp_c(), throttled=throttled(),
                                   status="moved" if moved else "ok")
                    w.writerow(row)
                    out.flush()
                    print(f"{n:2} {sc['name']:<14} {row['variant']:<8} {row['value']!s:>2} "
                          f"{row['value_name'][:24]:<24} {row.get('avg', '-'):>5} {row['throttled']} {row['status']}",
                          file=sys.stderr)
    finally:
        for n, name, v in restore:
            ctl.ask(f"sset {n} {name} {v:g}")
        ctl.ask(f"scene {start['scene']} fpssweep")
        if out is not sys.stdout:
            out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
