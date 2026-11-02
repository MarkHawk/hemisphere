"""Album art for the dome: download, cache, resize, and pick a palette.

Covers are fetched once (http and https only, 5 s and 5 MB at most),
kept in a cache folder by the hash of their address, and turned into
what scenes read straight into a texture, with no decoding on the Pi's
render thread:

    art.rgba     512 x 512, 4 bytes a pixel, top row first
    thumb.rgba   128 x 128, the same, for the album atlas

512 because the Pi 3's GPU (VC4, OpenGL ES 2.0) only mipmaps and wraps
power-of-two textures, the sources give 300 to 640 pixels, and the
front of the globe is about 600 pixels across: 1024 would add no detail
and cost four times the memory and upload time.

The palette is four colours, brightest first, from the cover: shrunk,
reduced to eight colours, and the four that cover most while differing
most and being colourful are kept. The darkest is lifted off black so a
theme built from it never goes dead.

Nothing here is ever committed: the cache and the files above live in
data/nowplaying/, which .gitignore leaves out.
"""
import colorsys
import hashlib
import os
import pathlib
import urllib.request

from PIL import Image

ART_SIZE = 512
THUMB_SIZE = 128
MAX_BYTES = 5 * 1024 * 1024
TIMEOUT = 5.0
CACHE_LIMIT = 50 * 1024 * 1024

Image.MAX_IMAGE_PIXELS = 40_000_000       # refuse absurd pictures


class ArtError(Exception):
    pass


def cache_path(url, cache_dir):
    return pathlib.Path(cache_dir) / (hashlib.sha1(url.encode()).hexdigest() + ".img")


def fetch(url, cache_dir, timeout=TIMEOUT, max_bytes=MAX_BYTES):
    """The cover at url, from the cache or downloaded into it."""
    if not url.lower().startswith(("http://", "https://")):
        raise ArtError("only http and https addresses are fetched")
    path = cache_path(url, cache_dir)
    if path.exists():
        os.utime(path)                       # recently used, for prune()
        return path
    req = urllib.request.Request(url, headers={"User-Agent": "hemisphere-nowplaying"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            ctype = r.headers.get("Content-Type", "")
            if ctype and not ctype.lower().startswith("image/"):
                raise ArtError(f"not a picture ({ctype})")
            body = r.read(max_bytes + 1)
    except OSError as e:
        raise ArtError(f"download failed: {e}") from e
    if len(body) > max_bytes:
        raise ArtError("picture too big")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".part")
    tmp.write_bytes(body)
    os.replace(tmp, path)
    return path


def prune(cache_dir, limit=CACHE_LIMIT):
    """Delete the least recently used covers beyond limit bytes."""
    files = sorted(pathlib.Path(cache_dir).glob("*.img"), key=lambda p: p.stat().st_mtime,
                   reverse=True)
    total = 0
    for f in files:
        total += f.stat().st_size
        if total > limit:
            f.unlink(missing_ok=True)


def square(img, size):
    """Centre-crop to a square and resize."""
    w, h = img.size
    s = min(w, h)
    img = img.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
    return img.resize((size, size), Image.LANCZOS)


def write_rgba(img, path):
    """Raw RGBA, top row first, written so a reader never sees half."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    tmp.write_bytes(img.convert("RGBA").tobytes())
    os.replace(tmp, path)


def _luma(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def _dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


ACCENT_MIN_SHARE = 0.003                  # about 12 pixels of the 64 x 64


def _fill(picked, n):
    """Too few distinct colours (a black and silver cover): add steps
    from the lightest to the darkest, so the theme still has a range."""
    picked = list(picked) or [(128, 128, 128)]
    hi, lo = max(picked, key=_luma), min(picked, key=_luma)
    if _dist(lo, hi) < 60:                    # about one colour: widen it
        hi = tuple(min(255, int(v * 1.6 + 40)) for v in hi)
        lo = tuple(int(v * 0.35) for v in lo)
        for c in (hi, lo):
            if len(picked) < n and c not in picked:
                picked.append(c)
    missing = n - len(picked)
    for k in range(1, missing + 1):
        t = k / (missing + 1)
        picked.append(tuple(int(round(h + (l - h) * t)) for h, l in zip(hi, lo)))
    return picked


def _accent(small):
    """A vivid colour the cover has, however small its area (at least
    ACCENT_MIN_SHARE), found from the pixels themselves: reducing to a
    few colours would blend a small one away. The most common vivid hue
    wins; its pixels are averaged. None if there is no vivid colour."""
    bins = {}
    raw = small.tobytes()
    for i in range(0, len(raw), 3):
        rgb = (raw[i], raw[i + 1], raw[i + 2])
        h, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in rgb))
        if s >= 0.45 and v >= 0.30:
            bins.setdefault(int(h * 12) % 12, []).append(rgb)
    if not bins:
        return None
    best = max(bins.values(), key=len)
    if len(best) < ACCENT_MIN_SHARE * small.size[0] * small.size[1]:
        return None
    return tuple(int(round(sum(c[k] for c in best) / len(best))) for k in range(3))


def palette(img, n=4):
    """n colours from a picture, brightest first, each (r, g, b) 0..255."""
    small = img.convert("RGB").resize((64, 64), Image.BILINEAR)
    q = small.quantize(colors=16, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()
    counts = sorted(q.getcolors(), reverse=True)
    total = sum(c for c, _ in counts)
    cands = []
    for count, idx in counts:
        rgb = tuple(pal[idx * 3: idx * 3 + 3])
        s = colorsys.rgb_to_hsv(*(x / 255 for x in rgb))[1]
        cands.append((count / total * (0.5 + s), rgb))
    cands.sort(reverse=True)
    accent = _accent(small)
    picked = []
    for gap in (70, 45, 25):                  # relax, but never to near-copies
        for _, rgb in cands:
            if len(picked) == n:
                break
            if rgb not in picked and all(_dist(rgb, p) >= gap for p in picked):
                picked.append(rgb)
        if len(picked) == n:
            break
    if accent and not any(_dist(accent, p) < 40 for p in picked):
        if len(picked) == n:
            picked[-1] = accent               # in place of the least important
        else:
            picked.append(accent)
    if len(picked) < n:
        picked = _fill(picked, n)
    picked.sort(key=_luma, reverse=True)
    # lift the darkest off black, keeping its hue
    out = []
    for rgb in picked:
        lum = _luma(rgb)
        if lum < 22:
            if lum < 1:
                rgb = (22, 22, 26)
            else:
                k = 22 / lum
                rgb = tuple(min(255, int(round(v * k))) for v in rgb)
        out.append(rgb)
    return out


def hexes(colours):
    return [f"{r:02x}{g:02x}{b:02x}" for r, g, b in colours]


def prepare(src, out_dir):
    """Turn a downloaded cover into art.rgba and thumb.rgba in out_dir.
    Returns the palette."""
    try:
        with Image.open(src) as im:
            im.load()
            img = im.convert("RGB")
    except (OSError, Image.DecompressionBombError) as e:
        raise ArtError(f"cannot read the picture: {e}") from e
    out_dir = pathlib.Path(out_dir)
    write_rgba(square(img, ART_SIZE), out_dir / "art.rgba")
    write_rgba(square(img, THUMB_SIZE), out_dir / "thumb.rgba")
    return palette(img)
