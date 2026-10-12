# Hemisphere: notes for Claude Code

Spherical display framework for the Gakken WorldEye (a 180 degree dome, driven by a
Raspberry Pi 3 over HDMI). Grew out of `hobby-builds/worldeye`, which is now frozen;
all new display work happens here. This is its own repo: `github.com/MarkHawk/hemisphere`,
local copy `D:\portfolio\hemisphere`. It is **not** part of hobby-builds.

## Layout

- `main.c`: entry point, scene switching
- `dome/`: the framework: lens mapping and calibration (`dome.c`, `dome.h`),
  shaders (`shaders.h`), text rendering (`text.c`, `font5x7.h`)
- `scenes/`: one file per scene (deathstar, planet, plasma, julia, kaleido, life,
  moon, orbit, groovy, scifi, eye, storms)
- `calibration.cfg`: how this WorldEye's lens fits the dome (device settings)
- `deathstar.cfg`: look settings for the Death Star scene
- `textures/`: planet maps from Solar System Scope (CC BY 4.0, credited in README)
- `third_party/stb_image.h`: image loading (public domain / MIT)
- `tools/`: `orbitfeed.py` + `orbitfeed.service` (Python, venv), `prep_textures.py`
- `deploy.sh`: copies the project to the Pi

## Build and deploy

- Built on the Pi with `make`; the program is `hemisphere`.
- Every scene file must be listed in the Makefile's `SRC`. `scenes/eye.c` and
  `scenes/storms.c` are currently not listed: either add them or mark them as work
  in progress in the README.
- To deploy, pass your Pi's details: `PI=user@your-pi DEST=/home/user/hemisphere ./deploy.sh`.
  Committed files keep the generic defaults (`pi@raspberrypi.local`, `/home/pi`).

## Rules

- Calibration (device) and look (scene) settings stay separate. Never change the
  lens mapping maths to fix how one scene looks.
- `*.cfg` files are committed on purpose.
- No hardcoded IPs, usernames or home paths in committed files.
- Keep the README's controls, settings and credits in step with the code.
- Commit in small steps. Never push: I push.
- C for rendering, Python (venv) for tools, Bash for scripts. State the language per
  new file and wait for my OK before writing it.
