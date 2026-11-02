"""Art and palette, with pictures drawn in the test (no real covers)."""
import pytest
from PIL import Image, ImageDraw

import art
from fakeheos import ArtServer


@pytest.fixture
def server():
    s = ArtServer().start()
    yield s
    s.stop()


def test_fetch_caches(server, tmp_path):
    url = server.url("/cover/1.png")
    p1 = art.fetch(url, tmp_path)
    p2 = art.fetch(url, tmp_path)
    assert p1 == p2 and p1.exists()
    assert server.hits.count("/cover/1.png") == 1


def test_fetch_refuses(server, tmp_path):
    with pytest.raises(art.ArtError, match="not a picture"):
        art.fetch(server.url("/text"), tmp_path)
    with pytest.raises(art.ArtError, match="too big"):
        art.fetch(server.url("/big"), tmp_path)
    with pytest.raises(art.ArtError, match="download failed"):
        art.fetch(server.url("/missing"), tmp_path)
    with pytest.raises(art.ArtError, match="only http"):
        art.fetch("file:///etc/passwd", tmp_path)
    assert list(tmp_path.glob("*")) == []


def test_prepare_writes_square_textures(tmp_path):
    img = Image.new("RGB", (800, 600), (200, 30, 30))
    ImageDraw.Draw(img).rectangle([0, 0, 99, 599], fill=(0, 0, 255))   # cropped away
    src = tmp_path / "c.png"
    img.save(src)
    pal = art.prepare(src, tmp_path / "out")
    data = (tmp_path / "out" / "art.rgba").read_bytes()
    assert len(data) == 512 * 512 * 4
    assert len((tmp_path / "out" / "thumb.rgba").read_bytes()) == 128 * 128 * 4
    assert data[:4] == bytes([200, 30, 30, 255])     # the blue strip was cropped off
    assert len(pal) == 4


def test_prepare_rejects_junk(tmp_path):
    src = tmp_path / "junk.img"
    src.write_bytes(b"not an image")
    with pytest.raises(art.ArtError):
        art.prepare(src, tmp_path / "out")


def test_palette_finds_the_main_colours():
    img = Image.new("RGB", (100, 100), (20, 40, 160))          # blue, most
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 49, 29], fill=(240, 200, 40))           # yellow
    d.rectangle([50, 0, 99, 19], fill=(200, 30, 60))           # red
    d.rectangle([0, 80, 99, 99], fill=(10, 10, 10))            # near black
    pal = art.palette(img)
    assert len(pal) == 4
    lum = [art._luma(c) for c in pal]
    assert lum == sorted(lum, reverse=True)                    # brightest first
    near = lambda a, b: art._dist(a, b) < 40
    assert any(near(c, (240, 200, 40)) for c in pal)
    assert any(near(c, (20, 40, 160)) for c in pal)
    assert any(near(c, (200, 30, 60)) for c in pal)
    assert min(lum) >= 21                                      # lifted off black


def test_palette_of_one_colour_still_gives_four():
    pal = art.palette(Image.new("RGB", (50, 50), (0, 0, 0)))
    assert len(pal) == 4 and all(art._luma(c) >= 21 for c in pal)
    assert art.hexes([(255, 0, 16)]) == ["ff0010"]


def test_prune_keeps_the_newest(tmp_path):
    import os
    import time
    for i in range(5):
        p = tmp_path / f"{i}.img"
        p.write_bytes(b"x" * 1000)
        os.utime(p, (time.time() - 100 + i, time.time() - 100 + i))
    art.prune(tmp_path, limit=2500)
    assert sorted(p.name for p in tmp_path.glob("*.img")) == ["3.img", "4.img"]
