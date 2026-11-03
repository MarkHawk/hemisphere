# Hemisphere: a framework for the Gakken WorldEye

https://github.com/user-attachments/assets/eac9f6f6-ce7c-4442-ab63-b2b5ab97cd12

Hemisphere drives the Gakken WorldEye spherical display from a Raspberry
Pi 3. The screen is a 180 degree dome, lit from inside through a fisheye
lens. The core owns the lens calibration and everything else every project
shares. A new visual is just a **scene**: a page or two of C and GLSL.

## Why I built this

The Gakken WorldEye is an interesting product, and surprisingly little has
been made for it. I wanted to drive mine from a Raspberry Pi 3 with my own
visuals, starting with a Death Star. I chose the Pi 3 on purpose, to
optimise for cheap hardware (and I had one lying around).

The first version didn't work. It rendered the model with a normal camera,
then fisheye-warped the image. That left a small flat picture in the middle
of the dome. No tuning could fix it, because the approach was wrong. From
photos of the real globe, I worked out the geometry. Then I mapped every
point straight onto the dome through a model of the lens. I built a
calibration grid and settings to measure and tune the physical device. A
strip of paper on the globe was the test equipment.

With the calibration right, I realised a framework others could use would
be worth more than one Death Star, so I separated the two. Each new visual
became a small scene file. It grew from one Death Star into the scenes
below, with live satellite, earthquake and weather data, sound from WLED
lights and a web remote. It began as my single-file `worldeye` program,
and that program's settings files still load.

It reflects how I work: reproduce the problem on the real hardware, find
the root cause rather than tuning around it, test against measurements,
and write it up (see the [debugging notes](docs/debugging-notes.md)).

## Scenes

Keys 1 to 9 and 0 pick the first ten. `,` and `.` step through them all,
and the [web remote](docs/controls.md#web-remote) has a button for each.
Every scene has several styles. Their settings are in
[docs/settings.md](docs/settings.md).

| Key | Scene | What it shows |
|-----|-------|---------------|
| 1 or D | Death Star | Panelled hull, superlaser dish and trench, under a wireframe that can bend to music |
| 2 | Planet | Any planet map, with day and night, city lights and an atmosphere glow |
| 3 | Plasma | Flowing colour in eight themes. The template for new scenes |
| 4 | Julia | Julia and Mandelbrot sets on the Riemann sphere, slowly morphing |
| 5 | Kaleidoscope | A mirrored folding fractal, turning inside the mirrors |
| 6 | Game of Life | Life on a globe with no edges, under three sets of rules |
| 7 | Moon tonight | The Moon at tonight's real phase, with the next full and new moon. No internet needed |
| 8 | Orbit watch | The live Earth, the ISS and its next orbit, a few hundred satellites and the day's earthquakes |
| 9 | Groovy | 60s and 70s patterns: op art, lava lamp, tie-dye, spirograph and more |
| 0 | Sci-fi | Hologram globe, radar with real satellites, hyperspace, reactor core, ship's computer |
| `,` `.` | Computers | Front panel, oscilloscope, code listing, core memory, packet network, blinkenlights |
| `,` `.` | Hypnotic | Tesseract, quasicrystal, Fibonacci globe, geodesic sphere and six more |
| `,` `.` | Sports balls | Ten balls rolling, plus a puzzle cube and a megaminx that solve themselves |
| `,` `.` | 8-bit retro | Demo effects, tape loading, and three games that play themselves across the dome |
| `,` `.` | Retro screen | 80s film and TV screens, from defence command to a TV closedown |
| `,` `.` | Weather | Today's clouds, the aurora forecast, and famous storms replayed |
| `,` `.` | Eyes | One big eye looking round the room: human, cat, owl, reptile and a fire eye |
| `,` `.` | Music | 21 styles that move to [sound from your WLED lights](docs/sound.md#sound-from-wled) |
| `,` `.` | Now playing | What your [HEOS amp](docs/sound.md#now-playing-from-a-heos-amp) is playing: the cover, a turning record, today's covers as an atlas |

## Gallery

| | | |
|:-:|:-:|:-:|
| ![Death Star](docs/screenshots/deathstar.jpg)<br>Death Star | ![Planet](docs/screenshots/planet.jpg)<br>Planet | ![Plasma](docs/screenshots/plasma.jpg)<br>Plasma |
| ![Julia](docs/screenshots/julia.jpg)<br>Julia | ![Kaleidoscope](docs/screenshots/kaleido.jpg)<br>Kaleidoscope | ![Game of Life](docs/screenshots/life.jpg)<br>Game of Life |
| ![Moon tonight](docs/screenshots/moon.jpg)<br>Moon tonight | ![Orbit watch](docs/screenshots/orbit.jpg)<br>Orbit watch | ![Groovy](docs/screenshots/groovy.jpg)<br>Groovy |
| ![Sci-fi](docs/screenshots/scifi.jpg)<br>Sci-fi | ![Computers](docs/screenshots/computers.jpg)<br>Computers | ![Hypnotic](docs/screenshots/hypno.jpg)<br>Hypnotic |
| ![Sports balls](docs/screenshots/balls.jpg)<br>Sports balls | ![8-bit retro](docs/screenshots/retro.jpg)<br>8-bit retro | ![Retro screen](docs/screenshots/screen.jpg)<br>Retro screen |
| ![Calibration grid](docs/screenshots/calibration.jpg)<br>Lens grid | | |

Photos of the real globe, taken in a dark room with the phone's exposure
turned down a little so the colours don't burn out. Weather, Eyes, Music
and Now playing have no photo yet.

## Quick start

**You need** a Gakken WorldEye, a Raspberry Pi 3 with 64-bit Raspberry Pi
OS, and an HDMI cable between them.

**Build and run** on the Pi:

```bash
sudo apt install libsdl2-dev libgles2-mesa-dev
git clone https://github.com/MarkHawk/hemisphere && cd hemisphere
make
DISPLAY=:0 ./hemisphere
```

The first build takes about half a minute. It opens on the Death Star.
**H** prints the keys and ESC quits.

**Calibrate it once.** Press **G** for the lens grid. TAB through the
settings and adjust each with LEFT and RIGHT until the rings sit round and
evenly spaced on the dome. **S** saves. The
[calibration guide](docs/calibration.md) gives the order and what "right"
looks like.

To copy code from another computer, adjust settings over SSH and start it
at boot, see [running on a Pi](docs/running.md). For live data, sound and
the web remote, see the pages below.

### Windows PC

Windows support: built for it, not yet tested on real hardware. See
[docs/windows.md](docs/windows.md).

## Go deeper

- [Running on a Pi](docs/running.md): building, copying code across, planet maps, starting at boot.
- [Calibration and how it draws](docs/calibration.md): the grids, the calibration steps, and the lens maths.
- [Settings](docs/settings.md): every scene's settings and styles, and the files they live in.
- [Controls and the web remote](docs/controls.md): the keys, the phone remote, its playlist and its API.
- [Live data](docs/live-data.md): the orbit feed for satellites, earthquakes and weather.
- [Sound and Now playing](docs/sound.md): reacting to WLED lights, and showing what a HEOS amp is playing.
- [Writing scenes](docs/writing-scenes.md): the layout, how a scene is built, keeping it quick on a Pi 3, testing switches.
- [Windows PC](docs/windows.md): the untested Windows build, the WorldEye as a second screen.
- [Debugging notes](docs/debugging-notes.md): problems found on the real hardware, and their root causes.

## Credits

* Planet and Moon maps: [Solar System Scope](https://www.solarsystemscope.com/textures/),
  licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
* Image loading: [stb_image](https://github.com/nothings/stb) by Sean
  Barrett, public domain.
* Dot-matrix font: the X11 misc-fixed 5x7 font, public domain.
* Moon phases: Jean Meeus, "Astronomical Algorithms".
* Satellite orbits from [CelesTrak](https://celestrak.org), worked out
  with the [sgp4](https://pypi.org/project/sgp4/) library; earthquakes from
  the [USGS](https://earthquake.usgs.gov).
* Live clouds: [Matt Eason's live cloud maps](https://github.com/matteason/live-cloud-maps)
  (CC0). Contains modified EUMETSAT data.
* Aurora forecast: [NOAA Space Weather Prediction Center](https://www.swpc.noaa.gov/products/aurora-30-minute-forecast) (OVATION model).
* Storm tracks: [NOAA NCEI IBTrACS](https://www.ncei.noaa.gov/products/international-best-track-archive), version 4.
* Sound: [WLED](https://kno.wled.ge)'s AudioReactive Audio Sync (V2). No
  WLED code is included.
* Now playing: the HEOS CLI Protocol Specification (Denon); covers and
  palettes with [Pillow](https://python-pillow.org) (MIT-CMU licence).
  Album covers belong to their owners and are only shown live, never
  stored in the repo.

## How it was built

I built it with AI-assisted tooling (Claude Code). I directed the work,
tested every change on the real display, and did the calibration myself.
Built with AI-assisted tooling (Claude Code), the now-playing tool included.

## Licence

[MIT](LICENSE)
