#!/usr/bin/env python3
"""Ramp hemisphere-bench one kind of work at a time and record the frame rate.

Run it on the Pi with hemisphere-bench running in place of Hemisphere
(make bench; stop Hemisphere first, as both want the screen and the
control channel):

    venv/bin/python tools/benchrun.py --out bench.csv
    venv/bin/python tools/benchrun.py --kinds sin,dots

Each step sets the bench's kind and level, checks with "state" that both
landed, waits for the frame rate to settle, then reads "state" every
couple of seconds, as tools/fpssweep.py does. Each row records min,
average and max fps, the bench's CPU time per frame (cpu_ms), the lowest
ARM, core and v3d clocks seen, the highest temperature and get_throttled.

A ramp stops at its first step under --stop-below fps (default 50). The
whole run stops if the temperature passes --max-temp (default 80 C) or
any throttle bit sets. The bench is put back to its plain surface at the
end. Standard library only.
"""
import argparse
import csv
import re
import subprocess
import sys
import time

from fpssweep import Ctl, param, temp_c, throttled

# kind number in bench.c, and the levels to step through
RAMPS = {
    "base":    (0, [1]),
    "sin":     (1, [1, 2, 4, 6, 8, 10, 12, 15, 20, 25, 30]),
    "trig":    (2, [1, 2, 3, 4, 5, 6, 7, 8]),
    "cells":   (3, [1, 2, 3, 4, 5, 6]),
    "baked":   (4, [1, 2, 3, 4, 5, 6, 7, 8]),
    "n3":      (5, [1, 2, 3, 4, 5, 6]),
    "texture": (6, [1, 2, 3, 4, 5, 6, 7, 8]),
    "theme":   (7, [1, 2, 3, 4, 6, 8]),
    "dots":    (8, [10, 20, 40, 60, 80, 100, 150, 200]),
    "upload":  (9, [1, 2, 3, 4, 5, 6, 7]),
    "busy":    (10, [2, 4, 6, 8, 10, 12, 14, 16]),
    "branch":  (11, [1, 2, 3, 4, 5]),
    "map":     (12, [1, 2, 3, 4, 6, 8]),
    "mapmip":  (13, [1, 2, 3, 4, 6, 8]),
    "maps":    (14, [1, 2, 3, 4, 5, 6]),
    "mapsmip": (15, [1, 2, 3, 4, 5, 6]),
    "formats": (16, [1, 2, 3, 4, 5, 6]),
}
# ramps that are a set of cases, not more and more work: never cut short
NO_STOP = {"branch", "formats"}
BRANCH_NAMES = ["setting off", "no pixel", "10% patch", "10% scattered", "every pixel"]
FORMAT_NAMES = ["luminance", "RGB", "RGBA", "lum-alpha half", "lum 1137x728", "Weather mix"]
UPLOAD_NAMES = ["64x64", "128x128", "256x256", "512x512", "640x474",
                "1024x1024", "full screen"]

FIELDS = ["kind", "level", "level_name", "samples", "min", "avg", "max", "cpu_ms",
          "arm_mhz", "core_mhz", "v3d_mhz", "temp_c", "throttled", "status", "time"]


def clock_mhz(name: str) -> float | None:
    try:
        out = subprocess.run(["vcgencmd", "measure_clock", name], capture_output=True,
                             text=True, timeout=2).stdout
        return int(out.strip().split("=")[-1]) / 1e6
    except Exception:
        return None


def show(ctl: Ctl, kind: int, level: int, tries: int = 3) -> bool:
    """Set the bench's kind and level and check that both landed."""
    for _ in range(tries):
        ctl.ask(f"sset 0 kind {kind}")
        ctl.ask(f"sset 0 level {level}")
        time.sleep(0.5)
        st = ctl.json("state")
        if st.get("name") == "BENCH" and param(st, "kind") == kind and param(st, "level") == level:
            return True
    return False


def measure(ctl: Ctl, seconds: float, every: float) -> dict:
    fps, cpu, temps, flags = [], [], [], set()
    clocks = {"arm": [], "core": [], "v3d": []}
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        time.sleep(every)
        st = ctl.json("state")
        fps.append(float(st["fps"]))
        cpu.append(float(param(st, "cpu_ms") or 0.0))
        for name, seen in clocks.items():
            mhz = clock_mhz(name)
            if mhz is not None:
                seen.append(mhz)
        t = temp_c()
        if t:
            temps.append(float(t))
        flags.add(throttled())
    flags.discard("")
    bad = sorted(f for f in flags if f != "0x0")

    def low(name: str) -> str:
        return f"{min(clocks[name]):.0f}" if clocks[name] else ""

    return {"samples": len(fps), "min": f"{min(fps):.1f}", "avg": f"{sum(fps) / len(fps):.1f}",
            "max": f"{max(fps):.1f}", "cpu_ms": f"{sum(cpu) / len(cpu):.1f}",
            "arm_mhz": low("arm"), "core_mhz": low("core"), "v3d_mhz": low("v3d"),
            "temp_c": f"{max(temps):.1f}" if temps else "",
            "throttled": ",".join(bad) if bad else "0x0"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=5005, help="control channel port (default 5005)")
    ap.add_argument("--out", default="-", help="CSV file to write (default: standard output)")
    ap.add_argument("--kinds", help="only these ramps, comma separated: " + ",".join(RAMPS))
    ap.add_argument("--settle", type=float, default=5.0, help="seconds before measuring (default 5)")
    ap.add_argument("--measure", type=float, default=20.0, help="seconds to measure (default 20)")
    ap.add_argument("--every", type=float, default=2.0,
                    help="seconds between readings (default 2, the bench's window)")
    ap.add_argument("--stop-below", type=float, default=50.0,
                    help="end a ramp at its first step under this average fps (default 50)")
    ap.add_argument("--max-temp", type=float, default=80.0,
                    help="stop the whole run above this temperature, C (default 80)")
    args = ap.parse_args()

    kinds = args.kinds.split(",") if args.kinds else list(RAMPS)
    for k in kinds:
        if k not in RAMPS:
            sys.exit(f"--kinds: no ramp {k!r}; choose from {','.join(RAMPS)}")

    ctl = Ctl(args.port, timeout=5.0)   # a big shader can take a moment to build
    if ctl.json("state").get("name") != "BENCH":
        sys.exit("the program on the control channel is not hemisphere-bench")

    out = sys.stdout if args.out == "-" else open(args.out, "w", newline="")
    w = csv.DictWriter(out, fieldnames=FIELDS)
    w.writeheader()
    stopped = ""
    try:
        for name in kinds:
            kind, levels = RAMPS[name]
            for level in levels:
                row = {"kind": name, "level": level,
                       "level_name": UPLOAD_NAMES[level - 1] if name == "upload"
                       else BRANCH_NAMES[level - 1] if name == "branch"
                       else FORMAT_NAMES[level - 1] if name == "formats" else "",
                       "time": time.strftime("%H:%M:%S")}
                if not show(ctl, kind, level):
                    row.update(samples=0, status="FAILED", temp_c=temp_c(), throttled=throttled())
                else:
                    time.sleep(args.settle)
                    row.update(measure(ctl, args.measure, args.every), status="ok")
                if row["throttled"] not in ("0x0", ""):
                    stopped = f"throttle bits set ({row['throttled']})"
                elif row["temp_c"] and float(row["temp_c"]) > args.max_temp:
                    stopped = f"temperature {row['temp_c']} C passed {args.max_temp:g} C"
                if stopped:
                    row["status"] = "STOPPED"
                w.writerow(row)
                out.flush()
                print(f"{name:<8} {level:>4} {row['level_name']:<11} {row.get('avg', '-'):>5} fps "
                      f"cpu {row.get('cpu_ms', '-'):>5} ms  arm {row.get('arm_mhz', '-'):>4} "
                      f"core {row.get('core_mhz', '-'):>3} v3d {row.get('v3d_mhz', '-'):>3}  "
                      f"{row['temp_c']} C {row['throttled']} {row['status']}", file=sys.stderr)
                if stopped:
                    break
                if row["status"] == "ok" and float(row["avg"]) < args.stop_below \
                        and name not in NO_STOP:
                    break
            if stopped:
                print(f"run stopped: {stopped}", file=sys.stderr)
                break
    finally:
        ctl.ask("sset 0 kind 0")
        ctl.ask("sset 0 level 1")
        if out is not sys.stdout:
            out.close()
    return 2 if stopped else 0


if __name__ == "__main__":
    sys.exit(main())
