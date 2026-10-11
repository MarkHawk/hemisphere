#!/usr/bin/env python3
"""Resize planet maps for the Hemisphere planet scene.

The Pi 3's GPU can't load textures bigger than 2048 pixels, and the planet
scene expects equirectangular maps (twice as wide as tall, longitude -180
to +180 left to right, north at the top). This script fits each map to
2048x1024 (or --width), keeps it 2:1, and saves it as a JPEG.

    python3 -m venv venv
    venv/bin/pip install -r tools/requirements.txt
    venv/bin/python3 tools/prep_textures.py ~/Downloads/earth_8k.jpg

Good free maps: NASA Visible Earth "Blue Marble", and the Solar System
Scope texture set (Mars, Jupiter, Moon...).
"""
import argparse
import pathlib
import sys

from PIL import Image

Image.MAX_IMAGE_PIXELS = None  # NASA maps are huge, and that's fine


def main() -> int:
    here = pathlib.Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("images", nargs="+", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, default=here.parent / "textures",
                    help="output folder (default: textures/ beside the program)")
    ap.add_argument("--width", type=int, default=2048,
                    help="output width; height is half of it (default 2048)")
    args = ap.parse_args()

    width = min(max(args.width, 256), 2048)
    args.out.mkdir(parents=True, exist_ok=True)
    for src in args.images:
        try:
            im = Image.open(src).convert("RGB")
        except OSError as e:
            print(f"skip {src}: {e}", file=sys.stderr)
            continue
        w, h = im.size
        if abs(w / h - 2.0) > 0.05:
            print(f"warning: {src.name} is {w}x{h}, not 2:1; it will be stretched",
                  file=sys.stderr)
        im = im.resize((width, width // 2), Image.LANCZOS)
        dst = args.out / (src.stem + ".jpg")
        im.save(dst, quality=90, optimize=True)
        print(f"{src.name} {w}x{h} -> {dst} {width}x{width // 2}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
