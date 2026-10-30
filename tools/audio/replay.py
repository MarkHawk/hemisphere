#!/usr/bin/env python3
"""Replay a WLED audio capture as real Audio Sync V2 packets.

Lets you test Hemisphere's sound reactivity with no music playing. Each line
of the capture is sent as one 44-byte multicast packet, at its original
timing (bursts included), looping until Ctrl+C.

    python3 tools/audio/replay.py                  this machine only
    python3 tools/audio/replay.py --ttl 1          the whole network

The default --ttl 0 keeps the packets on this machine. With --ttl 1 every
WLED device set to receive audio sync will dance to the capture too.

Capture format, one packet per line (as in billie.txt):
    time  sampleSmth  samplePeak | 16 FFT bands, bass to treble
The capture lacks sampleRaw, the frame counter, FFT_Magnitude and
FFT_MajorPeak; they are filled in as described in make_packet().
Standard library only: no venv needed.
"""
import argparse
import pathlib
import socket
import struct
import sys
import time

# Rough centre of each of WLED's 16 bands, in Hz, for FFT_MajorPeak
BAND_HZ = (65, 107, 172, 258, 366, 495, 689, 969,
           1270, 1658, 2153, 2713, 3359, 4091, 5792, 8182)


def read_capture(path: pathlib.Path) -> list[tuple[float, float, int, list[int]]]:
    rows = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            head, bands = line.split("|")
            t, smth, peak = head.split()
            b = [int(x) for x in bands.split()]
        except ValueError:
            sys.exit(f"{path}:{n}: not a capture line: {line!r}")
        if len(b) != 16:
            sys.exit(f"{path}:{n}: expected 16 bands, found {len(b)}")
        rows.append((float(t), float(smth), int(peak), b))
    if not rows:
        sys.exit(f"{path}: empty")
    return rows


def make_packet(smth: float, peak: int, bands: list[int], counter: int) -> bytes:
    """WLED's audioSyncPacket V2, little-endian, 44 bytes:
        0 header "00002\\0"   6 pressure (2 bytes, 0 here)
        8 sampleRaw (= sampleSmth here)   12 sampleSmth
       16 samplePeak   17 frame counter   18 fftResult[16]
       34 zero crossings (0 here)   36 FFT_Magnitude   40 FFT_MajorPeak
    FFT_Magnitude is taken as the loudest band x 4 (roughly WLED's scale);
    FFT_MajorPeak is that band's centre, or 1.0 when silent, as WLED sends."""
    top = max(range(16), key=lambda i: bands[i])
    loud = bands[top]
    return struct.pack("<6s2sffBB16sHff", b"00002\0", b"\0\0", smth, smth,
                       peak & 0xFF, counter & 0xFF, bytes(bands), 0,
                       loud * 4.0, float(BAND_HZ[top]) if loud else 1.0)


def main() -> int:
    here = pathlib.Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--file", type=pathlib.Path, default=here / "billie.txt",
                    help="capture to replay (default: billie.txt beside this script)")
    ap.add_argument("--group", default="239.0.0.1", help="multicast group (WLED's is 239.0.0.1)")
    ap.add_argument("--port", type=int, default=11988, help="UDP port (WLED's default is 11988)")
    ap.add_argument("--ttl", type=int, default=0,
                    help="0 keeps packets on this machine (default), 1 sends them to the network")
    ap.add_argument("--speed", type=float, default=1.0, help="playback speed (default 1)")
    ap.add_argument("--once", action="store_true", help="play once instead of looping")
    args = ap.parse_args()
    if args.speed <= 0:
        sys.exit("--speed must be above 0")

    rows = read_capture(args.file)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, args.ttl)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    dest = (args.group, args.port)
    span = rows[-1][0] - rows[0][0]
    print(f"Replaying {args.file.name}: {len(rows)} packets, {span:.0f} s, "
          f"to {args.group}:{args.port}, ttl {args.ttl}. Ctrl+C stops.")

    counter = 0
    try:
        while True:
            start = time.monotonic()
            t0 = rows[0][0]
            for t, smth, peak, bands in rows:
                wait = start + (t - t0) / args.speed - time.monotonic()
                if wait > 0:
                    time.sleep(wait)
                s.sendto(make_packet(smth, peak, bands, counter), dest)
                counter += 1
            if args.once:
                break
            time.sleep(0.5)   # a short silence between loops, as between songs
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
