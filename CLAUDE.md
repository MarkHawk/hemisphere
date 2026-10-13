# Hemisphere: notes for Claude Code

Spherical display framework for the Gakken WorldEye (a 180 degree dome, driven by a
Raspberry Pi 3 over HDMI). Grew out of `hobby-builds/worldeye`, which is now frozen;
all new display work happens here. This is its own repo: `github.com/MarkHawk/hemisphere`,
local copy `D:\portfolio\hemisphere`. It is **not** part of hobby-builds.

## Layout

- `main.c`: entry point, scene switching. The 15 scenes, in `main.c` order:
  deathstar, planet, plasma, julia, kaleido, life, moon, orbit, groovy, scifi,
  groovy2, hypno, balls, retro, seasons. The first ten are on number keys 1 to
  9 and 0; the rest are on `,` and `.` and the web remote. The framework
  allows up to 16 scenes.
- `dome/`: the framework: lens mapping and calibration (`dome.c`, `dome.h`),
  the GL loader for Windows (`glload.c`, `glload.h`), shaders (`shaders.h`),
  text rendering (`text.c`, `font5x7.h`)
- `scenes/`: one file per scene (see the list above)
- `calibration.cfg`: how this WorldEye's lens fits the dome (device settings)
- `deathstar.cfg`: look settings for the Death Star scene
- `textures/`: planet maps from Solar System Scope (CC BY 4.0, credited in README)
- `third_party/stb_image.h`: image loading (public domain / MIT)
- `tools/`: `orbitfeed.py` + `orbitfeed.service` (Python, venv), `prep_textures.py`,
  `remote/` (a Flask web remote on port 5000, talking to the UDP control channel
  on 127.0.0.1:5005), `windows/` (start-up and autostart scripts)
- `docs/screenshots/`: gallery photos used in the README
- `deploy.sh`: copies the project to the Pi

## Build and deploy

- Built with `make`. On the Pi (or any Linux), the program is `hemisphere`. On
  Windows, in an MSYS2 UCRT64 terminal, the same `make` command builds
  `hemisphere.exe` and copies `SDL2.dll` alongside it.
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
- A new scene needs its file listed in `main.c` and the Makefile's `SRC`, a row
  in the README's settings table, and a gallery photo. Keep each scene quick on
  a Pi 3: check with `DOME_FPS=1`.
