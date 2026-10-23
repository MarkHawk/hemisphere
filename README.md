# Hemisphere: a framework for the Gakken WorldEye

https://github.com/user-attachments/assets/eac9f6f6-ce7c-4442-ab63-b2b5ab97cd12

Drives the Gakken WorldEye spherical display from a Raspberry Pi 3, or
from a Windows PC (see [Windows PC](#windows-pc)). The screen is a 180
degree dome lit from inside through a fisheye lens. The
core here owns the lens calibration and everything else that is the same
for every project, so a new visual is just a **scene**: a page or two of C
and GLSL.

## Why I built this

The Gakken WorldEye is an interesting product, and surprisingly little has
been made for it. I wanted to drive mine from a Raspberry Pi 3 with my own
visuals, starting with a Death Star. I chose the Pi 3 on purpose to
optimise for cheap hardware (and I had one lying around).

The first version didn't work. It rendered the model with a normal camera
and then fisheye-warped the image, leaving a small flat picture in the
middle of the dome. No tuning could fix that; the approach was wrong. From
photos of the real globe, I worked out the geometry and mapped every point
straight onto the dome through a model of the lens. I built a calibration
grid and settings to measure and tune the physical device. A strip of paper
on the globe was the test equipment.

With the calibration right, I realised a framework others could use would
be worth more than one Death Star, so I separated the two. Any new visual
became a small scene file. At the time of writing it has eleven scenes
(maybe more by now), live satellite and earthquake data, and a web remote.

It reflects how I work: reproduce the problem on the real hardware, find
the root cause rather than tuning around it, test against measurements, and
write it up (see [Debugging notes](#debugging-notes)).

I built it with AI-assisted tooling (Claude Code). I directed the work,
tested every change on the real display, and did the calibration myself.

Sixteen scenes come with it. The first ten are on the number keys; the
rest are on `,` and `.` and the web remote.

| Key | Scene | What it shows |
|-----|-------|---------------|
| 1 or D | Death Star | Lit, panelled hull with the green grid, superlaser dish and trench drawn over it |
| 2 | Planet | Any planet map wrapped round the globe, with day/night shading and an atmosphere glow. With no maps installed it shows a procedural gas giant |
| 3 | Plasma | A flowing full-colour field in eight colour themes, and the template for new scenes |
| 4 | Julia | A Julia set fractal wrapped round the globe on the Riemann sphere, morphing as its constant walks the edge of the Mandelbrot set. Also the Mandelbrot set itself, in eight colour themes |
| 5 | Kaleidoscope | A mirrored folding fractal: 48 identical tiles, or classic wedges round the pole, slowly turning inside the mirrors, in eight colour themes |
| 6 | Game of Life | Life on a globe with no edges, under three sets of rules and in eight colour themes. Fresh soup drops in whenever it settles down |
| 7 | Moon tonight | The Moon filling the dome, lit to tonight's real phase, with its age and the next full and new moon written across the equator. No internet needed |
| 8 | Orbit watch | The live Earth with real day and night, the ISS and its next orbit, a few hundred satellites, and the last day's earthquakes |
| 9 | Groovy | 60s and 70s patterns: op art, psychedelic swirl, flower power, lava lamp, liquid light, tie-dye, paisley, spirograph |
| 0 | Sci-fi | Hologram globe, radar sweep (with real satellites if the orbit feed is running), hyperspace, planet scan, reactor core, ship's computer |
| , and . | Computers | Classic computing: a front panel of switches and lamps with a program being toggled in, oscilloscope figures, a scrolling assembler listing, a circuit board, core memory, a packet network round the globe, and a globe of blinking lamp panels |
| , and . | Hypnotic | Tesseract, quasicrystal, moire, Fibonacci globe, Truchet maze, hypno spiral, tunnel, pendulum, breathing hexagons, geodesic sphere |
| , and . | Sports balls | The globe as a ball, rolling: football, basketball, tennis, cricket, baseball, volleyball, golf, pool 8-ball, beach ball, bowling ball, and two plastic puzzles that solve themselves: a puzzle cube (2x2 to 5x5) and a megaminx |
| , and . | 8-bit retro | Chunky-pixel effects: digital rain, copper bars with a sine scroller, starfield, pixel planet, a maze chase, Rim defender, Asteroids on a globe, 16-colour plasma, tape loading, pixel fire. The three games are full games that play themselves across the whole dome |
| , and . | Retro screen | 80s film and TV screens: a defence command globe with missile tracks, noughts and crosses played to a draw on 23 boards, a cyborg's visor, a motion tracker, a ship's computer terminal, light-cycles on a neon grid, and a TV closedown |
| , and . | Weather | Today's clouds over the turning Earth (still or drifting), an infrared-style picture, a sharp view zoomed in over home, tonight's aurora forecast round the pole, and famous hurricanes and typhoons replayed from the record |

Hemisphere grew out of my earlier `worldeye` program, a single-file Death
Star renderer. The lens calibration from that project is the core here,
and its settings files still load (see [Settings](#settings)).

See the [gallery](#gallery) for each scene on the globe.

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
turned down a little so the colours don't burn out.

## Layout

```
hemisphere/
  main.c                   lists the scenes, in key order
  Makefile
  dome/dome.h              the framework API (read this first)
  dome/dome.c              window, calibration, lens maths, settings, input
  dome/glload.h, glload.c  OpenGL ES functions on Windows (nothing to do on the Pi)
  dome/shaders.h           GLSL shared by every scene
  dome/text.c              dot-matrix text on the dome
  dome/font5x7.h           the font (X11 misc-fixed 5x7, public domain)
  scenes/deathstar.c       1   scenes/life.c     6
  scenes/planet.c          2   scenes/moon.c     7
  scenes/plasma.c          3   scenes/orbit.c    8
  scenes/julia.c           4   scenes/groovy.c   9
  scenes/kaleido.c         5   scenes/scifi.c    0
  scenes/computers.c       the rest are on , and .:
  scenes/hypno.c, balls.c, retro.c, screen.c
  third_party/stb_image.h  JPEG/PNG loader (public domain, single header)
  textures/                planet and moon maps go here
  data/                    live data from the orbit feed (created by it)
  tools/prep_textures.py   resizes maps for the Pi
  tools/orbitfeed.py       live satellite and earthquake data
  tools/orbitfeed.service  runs the feed at boot
  tools/remote/app.py      the web remote (Flask) and its API
  tools/remote/playlist.py the remote's playlist (runs in the background)
  tools/remote/static/     the remote's page
  tools/remote/remote.service  runs the remote at boot
  tools/requirements.txt   Python packages for the tools
  tools/windows/           start-up scripts for a Windows PC
  deploy.sh                copy to the Pi and build (Git Bash, WSL or Linux)
  docs/screenshots/        photos for the gallery
  calibration.cfg          calibration (my WorldEye)
  deathstar.cfg            Death Star look
```

## Build and run

Tested on a Raspberry Pi 3 with 64-bit Raspberry Pi OS, SDL2 and OpenGL
ES 2.0.

```bash
sudo apt install libsdl2-dev libgles2-mesa-dev
make
DISPLAY=:0 ./hemisphere
```

The first build takes roughly half a minute on a Pi 3, mostly for the image
loader; after that `make` only rebuilds what changed. At start-up the
scene fades in over two seconds.

### Copying from Windows

The examples here use `pi@raspberrypi.local` and `/home/pi`; put your own
Pi's user and address in their place. From PowerShell in the `hemisphere`
folder, copy the code across (this leaves the settings you have saved on
the Pi alone):

```powershell
scp main.c Makefile README.md pi@raspberrypi.local:~/hemisphere/
scp -r dome scenes tools third_party pi@raspberrypi.local:~/hemisphere/
scp textures\*.jpg pi@raspberrypi.local:~/hemisphere/textures/
```

then on the Pi: `pkill hemisphere; cd ~/hemisphere && make && DISPLAY=:0 ./hemisphere`.

The first time, copy the whole folder instead (`scp -r hemisphere
pi@raspberrypi.local:~/`). From Git Bash, WSL or Linux, `./deploy.sh` does
all of this in one go, and only copies settings files the Pi doesn't
already have. Tell it where your Pi is:
`PI=me@my-pi DEST=/home/me/hemisphere ./deploy.sh`.

Run it in the foreground in your SSH session while adjusting anything. It
prints each setting as you change it, and you can press keys right there in
the SSH window. Press **S** to save and **q** to quit. Quitting does not
save. It starts on the Death Star; press **G** for the calibration grids,
and **H** for a list of keys.

## How it draws

Every scene can draw two ways, and the Death Star uses both.

**Surface (full colour).** The scene writes one GLSL function:

```glsl
vec4 surface(vec3 dir, vec3 view)
```

`dir` is the point on the globe in the model's own coordinates (it turns
as the model spins; +Y is north). `view` is the same point fixed to the
globe on your desk (+Z out of the front, +Y up), for lighting. The core
runs this once for every pixel of the dome, so every pixel is filled:
solid planets, gradients, textures.

**Points.** Dots placed on the unit sphere, or recessed into it, mapped
onto the dome one by one. Use them for crisp lines, grids and overlays on
top of a surface.

### The lens maths

The projector's fisheye lens points straight out of the front of the dome.
For a point at angle phi from the dome's centre (0 at the centre, 90 at the
rim) and direction theta round it, the image radius is:

```
alpha(phi) = atan2(sin(phi), cos(phi) + offset)
rho(phi)   = radius * alpha(phi) / alpha(90 deg) * corr(phi)
```

`offset` is how far the lens sits behind the centre of the globe (0 means
at the centre), `radius` is where the rim lands on screen, and `corr` is
the ring30/ring60 fine correction.

Points use this forwards in the vertex shader. Surfaces need it backwards:
for this pixel, which point of the dome is it? The core tabulates
`rho(phi)`, inverts the table, and bakes the answer for every pixel into a
lookup texture whenever the calibration changes. Scenes never see the lens,
and dots and colour line up exactly because both come from the same
numbers. Press **O** in any scene to draw the globe grid over it and check.

The surface is rendered at `res` pixels across (640 by default) and
stretched onto the screen. That keeps the Pi 3 fast; raise `res` for a
sharper image if the frame rate holds up (`DOME_FPS=1 ./hemisphere` prints
it every ten seconds).

## Planet maps

The planet scene uses equirectangular maps: twice as wide as tall,
longitude -180 to +180 from left to right, north at the top. NASA's Blue
Marble and the Solar System Scope set (Mars, Jupiter, Moon and so on) are
good free sources. The Pi 3 can't load textures over 2048 pixels, so
resize them first:

```bash
python3 -m venv venv
venv/bin/pip install -r tools/requirements.txt
venv/bin/python3 tools/prep_textures.py ~/Downloads/earth_8k.jpg ~/Downloads/mars.jpg
```

They land in `textures/`. The 2K maps from
[Solar System Scope](https://www.solarsystemscope.com/textures/) are already
2048x1024, so they can go straight in. In the planet scene, **N** and **M**
step through them in alphabetical order. The Moon scene uses any map with
"moon" in its name, and orbit watch uses the Earth day and night maps.

**Night maps.** A map with "night" in its name is never shown on its own.
If the current map has "day" in its name and a matching file with
"night" in its place exists, such as `2k_earth_daymap.jpg` and
`2k_earth_nightmap.jpg`, its city lights glow on the dark side. The
`lights` setting sets how bright they are.

## Settings

Settings are kept in files next to the program, in groups:

| File | Group | Adjusted in | What it covers |
|------|-------|-------------|----------------|
| `calibration.cfg` | Calibration | Lens grid (G), globe grid (B) | How this WorldEye's lens fits the dome. Set once per device. |
| `view.cfg` | View | Any scene | Orientation and spin, shared by every scene |
| `deathstar.cfg` | Death Star | Death Star (1 or D) | The Death Star's look |
| `planet.cfg` | Planet | Planet (2) | Brightness, shading, atmosphere |
| `plasma.cfg` | Plasma | Plasma (3) | Theme, speed, scale, colour |
| `julia.cfg` | Julia | Julia (4) | Theme, Julia or Mandelbrot, shape, speed, detail |
| `kaleido.cfg` | Kaleidoscope | Kaleidoscope (5) | Theme, mirrors, fractal shape |
| `life.cfg` | Game of Life | Game of Life (6) | Theme, rules, grid size, speed, look |
| `moon.cfg` | Moon tonight | Moon (7) | Earthshine, rocking, text, text band |
| `orbit.cfg` | Orbit watch | Orbit watch (8) | View, home, what to show |
| `groovy.cfg` | Groovy | Groovy (9) | Style, speed, scale |
| `scifi.cfg` | Sci-fi | Sci-fi (0) | Style, speed, scale, radar range and centre |
| `computers.cfg` | Computers | Computers (, and .) | Style, speed, phosphor colour, text |
| `hypno.cfg` | Hypnotic | Hypnotic (, and .) | Style, colours, speed, scale |
| `balls.cfg` | Sports balls | Sports balls (, and .) | Ball, roll, shine |
| `retro.cfg` | 8-bit retro | 8-bit retro (, and .) | Style, speed, pixel size, pixel gaps |
| `screen.cfg` | Retro screen | Retro screen (, and .) | Style, speed, text |
| `weather.cfg` | Weather | Weather (, and .) | Style, home, zoom, clouds, coasts, storm speed |

In a scene, TAB steps through the six view settings first (`pitch`,
`tilt`, `wobble`, `rev_secs`, `res`, `view_dist`) and then that scene's
own, so press TAB six times to reach a scene's first setting. Calibration
is only reachable from the grids, so you can't knock it out while styling.
**S** saves all the files. **R** resets the group you are in: on a grid,
the calibration; in a scene, the view settings and that scene's own. **L**
reloads what you last saved. Settings a file has that the program no
longer uses are ignored, and the next save drops them.

**Coming from worldeye.** If `calibration.cfg` or `deathstar.cfg` is
missing, the old `worldeye.cfg` and `look.cfg` are read instead, including
the view settings that used to live in `look.cfg`. The first save writes
the new files, and the old ones can then be deleted. To move an existing
install on the Pi, copy the two old files into `/home/pi/hemisphere`.

**View settings:**

| Setting | What it does |
|---------|--------------|
| `pitch` | Tips the model towards you, moving the pole from the top edge of the dome to the top of the globe |
| `tilt` | Leans the axis sideways (0 upright, 23 Earth-like) |
| `wobble` | Slow nodding as it turns, in degrees (0 for steady) |
| `rev_secs` | Seconds per full turn |
| `res` | Colour resolution across the dome: lower is faster, higher is sharper |
| `view_dist` | How far away you stand, in globe radii. Sets how strongly recesses such as the dish show depth |
| `display` | Which screen the WorldEye is on: 0 is the main one. Only matters on a PC with more than one screen (V steps through them) |

The Moon, and the text in the Moon and orbit watch, follow `pitch` and
`tilt` but never spin. The lava lamp, the sunshine face, smiley rain and rainbow face, radar, hyperspace and ship's computer
are drawn square to the dome, ignoring the view settings.

**Death Star settings:** the scene opens with the wireframe zooming out from a
single dot at the centre of the dome until it fills the globe, then the hull
fades in; **Z** replays this and **C** cycles the wireframe colour.

| Setting | What it does |
|---------|--------------|
| `style` | 0 wireframe only, 1 hull and wireframe, 2 wireframe fades to hull after `wire_out` seconds, 3 hull only |
| `phosphor` | Wireframe colour: 0 green, 1 orange, 2 blue, 3 white (**C** cycles) |
| `lines` | Wireframe brightness. 0 gives a solid model with no grid |
| `dot_size` | Wireframe line thickness in pixels (**N**/**M** thinner/thicker); the dots always join up |
| `grid_deg` | Spacing of the wireframe grid in degrees |
| `dish_dep` | Depth of the superlaser dish, as a fraction of the globe's radius (0.10 is like the film; **I**/**K** shallower/deeper) |
| `hull` | Brightness of the solid hull |
| `detail` | Small hull detail: sub-plates, raised blocks, grooves (0 for plain plates) |
| `windows` | Brightness of the small lit windows, seen best on the dark side |
| `intro` | Zoom out from a dot when the scene starts (1 is on; **Z** replays it) |
| `intro_s` | Seconds the zoom out takes |
| `wire_out` | Style 2 only: seconds after the start when the wireframe fades away |

The hull's panelling, panel size, seams, dish position and pole dimming are
now Mark's chosen look, fixed in the code rather than adjustable settings.

**Planet settings:** `bright`, `sun` (0 lit all over, 1 sharp night side),
`night` (how much of the night side shows), `atmos` (rim glow), `bands`
(gas giant only), `lights` (city lights on the night side).

**Colour themes.** Plasma, Julia, the kaleidoscope and Life share eight
colour themes, and **N** steps through them: rainbow, fire, ice, neon,
aurora, gold, amber and jewel (stained-glass colours with dark leading). In
Life the first theme is its own classic colouring instead of rainbow. The
`hue` setting, where a scene has one, slides the colours along within a
theme.

**Plasma settings:** `theme`, `speed`, `scale`, `bands`, `sat`, `bright`.

**Julia settings:** `theme`, `set` (0 Julia, 1 Mandelbrot; **M** switches),
`edge` (under 1 gives solid shapes, over 1 lacy dust), `speed`, `iter`
(detail; more is slower, 32 to start), `zoom`, `hue`, `bands` (how fast the colours
cycle), `glow` (brightness inside the set). Infinity sits at the north
pole, so the top of the globe is the dark outside of the set. The
Mandelbrot set is the map of every Julia set: it stays still while the
globe turns it.

**Kaleidoscope settings:** `theme`, `style` (0 cube mirrors, 1 wedges;
**M** switches), `mirrors` (wedges, style 1), `fold` (the fractal's shape:
tiny changes give very different patterns, so step it with Shift for fine
steps), `depth`, `speed`, `hue`, `sat`, `bright`.

**Game of Life settings:** `theme`, `rule` (**M** steps through them, and
reseeds: 0 Conway's original B3/S23; 1 HighLife B36/S23, which adds
self-copying replicators; 2 Day & Night B3678/S34678, big blobs that grow
and shrink), `cells` (cells along each cube face edge; the globe has six
faces, so 48 gives 13,824 cells. Changing it reseeds), `speed` (generations
per second), `density` (how full a reseed is), `gap` (space between tiles),
`trail` (how long dead cells leave a ghost), `glow`, `hue`. Newborn cells
are the brightest, cooling as they age. **C** reseeds the whole globe. The
globe is laid out as an inflated cube, so at its eight corners three faces
meet and a cell has seven neighbours instead of eight.

**Moon settings:** `shine` (earthshine on the dark side), `rock` (a
slow, gentle rocking, like the Moon's real libration), `text` (0 hides the
text), `band` (how dark the band behind the text is), `bright`. The Moon
fills the whole dome, facing you, with north at the top of the globe as set
by the view settings (`pitch`, `tilt`). Uses any map with "moon" in its name
from `textures/`. Four lines across the equator give the phase and how much
is lit, the Moon's age, and the next full and new moon in local time. They
are worked out from the Pi's clock (Meeus, "Astronomical Algorithms", good
to a few minutes) and update every minute. To preview another night, start
it with `DOME_DAYS=5 ./hemisphere` for five days from now.

**Orbit watch settings:** `view` (0 turning, 1 following the ISS, 2 held
over home; **N** cycles), `home_lat` and `home_lon` (home for view 2),
`sats` and `quakes` (brightness; 0 hides them), `lights` (city lights),
`text`, `band` (how dark the band behind the text is), `bright`. Four
lines across the equator give the ISS's position, altitude and speed, and
the quake count and biggest magnitude. The text stays put while the Earth
turns, and satellites and quakes passing behind it are hidden so it stays
clear. The ISS is the bright pulsing marker, with its next orbit
in dashed cyan ahead of it and a short white trail behind. Satellites are
coloured by type: yellow space stations, white bright satellites, green GPS,
blue weather satellites. Earthquakes pulse as rings, yellow under M4, orange
to M6, red above; the last six hours are brighter. Home starts at Greenwich;
set `home_lat` and `home_lon` to your own location.

**Groovy settings:** `style` (**N** steps through them: 0 op art, 1
psychedelic swirl, 2 flower power, 3 lava lamp, 4 liquid light, 5 tie-dye,
6 paisley, 7 spirograph), `speed`, `scale` (bigger gives a finer pattern),
`bright`. Tie-dye winds five dyes into a spiral round a fold; every 40
seconds a new fold is dyed and spreads over the old one. The spirograph
draws four gear patterns out to the rim, showing the ring and the rolling
gear as it goes, then fades and starts a fresh sheet. The patterns turn
with the globe, except the lava lamp and spirograph, which face you. (The
style numbers changed in October 2026, when 70s wallpaper, mood ring,
mirror ball and glam stars were dropped; an old `groovy.cfg` just opens on
a different style.)

**Computers settings:** `style` (**N**: 0 front panel, 1 oscilloscope, 2
code listing, 3 circuit board, 4 core memory, 5 packet network, 6
blinkenlights), `speed`, `phosphor` (**M**: one screen colour for every
style, 0 green, 1 amber, 2 white, or 3 full colour, the default), `text` (labels
and readouts), `bright`. It has no number key: press `,` or `.` to reach
it, or pick it on the web remote. The front panel's switches are toggled
one at a time to load an address and deposit a short made-up program, word
by word, which then runs with the lamps flickering before it halts and
starts again. Core memory, blinkenlights and the packet network turn with
the globe; the rest face you.

**Hypnotic settings:** `style` (**N**: 0 tesseract, 1 quasicrystal, 2 moire,
3 Fibonacci globe, 4 Truchet maze, 5 hypno spiral, 6 tunnel, 7 pendulum, 8
breathing hexagons, 9 geodesic sphere), `theme` (**M**: 0 each style's own
colours, 1 to 8 the shared themes), `speed`, `scale`, `bright`. The tesseract
is a 4D cube turned through the fourth dimension and projected twice; the
quasicrystal is five plane waves at 36 degree steps, a pattern that never
repeats. The Fibonacci globe spreads 610 seeds evenly over the sphere, each
a golden angle round from the last, which forms crossing spirals of 21 and
34. The pendulum traces rosettes over the whole globe, and the geodesic
sphere has a coarser one turning the other way inside it, drawn recessed
so it shows depth. The moire, Fibonacci globe, pendulum and geodesic turn
with the globe; the rest face you.

**Sports balls settings:** `style` (**N**: 0 football, 1 basketball, 2
tennis, 3 cricket, 4 baseball, 5 volleyball, 6 golf, 7 pool 8-ball, 8 beach
ball, 9 bowling ball, 10 puzzle cube, 11 megaminx), `roll` (tumbling end
over end on top of the spin), `bright`, `shine` (the highlight), `size` (the
puzzle cube's layers: 2 to 5, or 1 for a different size each time it mixes
itself up). The puzzles are blown up
into a ball, glossy stickers on black plastic: they start mixed up, solve
themselves a turn at a time, glint when they're done, then mix themselves
up again. The megaminx is the twelve-sided puzzle, with straight-edged
pieces. (The "solve" is a replay: each puzzle undoes its own mixing moves in
reverse rather than working out a solution, and mixes itself with enough
turns to look properly mixed for its size.) The football is the real solid, a
truncated icosahedron: every point belongs to whichever face plane it
reaches first. The tennis and baseball seams follow the curve where height
equals x squared minus y squared.

**8-bit retro settings:** `style` (**N**: 0 digital rain, 1 copper bars, 2
starfield, 3 pixel planet, 4 maze chase, 5 Rim defender, 6 Asteroids, 7
16-colour plasma, 8 tape loading, 9 pixel fire), `speed`, `scale` (pixel
size), `bright`, `grid` (show the gaps between pixels). The three games are
full games, each played by a small robot in C, with lives, a score and high
score, levels that get harder, and GAME OVER then a fresh game.

* **Maze chase:** an original maze game. A little round robot clears the
  dots while four spiky chasers hunt it, each in its own way (one chases,
  one heads it off, one flanks, one wanders). Power cells turn them blue
  for a while so the robot can eat them; bonus prizes appear twice a level.
  The robot stays in the middle of the dome and the maze slides round it,
  repeating left and right so the side tunnel carries straight on. Levels
  get faster and there is an extra life at 10,000. The hidden treat:
  clear level 4 and level 5 is the planet maze, the whole maze laid over a
  small dark planet that turns under the robot, with every point doubled. The robot is an expert: it tries each way a couple of seconds
  ahead, with the chasers moving as they really would, so it usually gets
  well past level 10 before it slips up.
* **Rim defender:** the ship circles the rim with rapid fire, shooting
  inwards at aliens that spiral out from a hive at the centre in groups and
  fire back. The robot leads its shots and dodges incoming fire.
* **Asteroids:** the whole globe is the playfield, so rocks and shots
  travel round the sphere and come back over the horizon, and the view
  follows the ship. Big rocks split into medium and then small ones;
  saucers turn up and shoot (the small one aims). The robot aims ahead,
  thrusts clear of trouble and jumps to hyperspace in a tight spot. An
  extra ship every 10,000 points.

The aliens, ships and pictures are made up for this program.

**Retro screen settings:** `style` (**N**: 0 defence command, 1 noughts
and crosses, 2 cyborg HUD, 3 motion tracker, 4 ship's terminal, 5 light
grid, 6 closedown), `speed`, `text` (the readouts round
the rim), `bright`. It has no number key: press `,` or `.` to reach it, or
pick it on the web remote. Defence command draws the coastlines from the
Earth map in `textures/` (made-up continents if there isn't one), holds the
globe over the North Atlantic so Europe and North America face you, and runs
a 66-second simulation from DEFCON 5 to 1 that ends with no winner. In
noughts and crosses both sides play perfectly, so every game is a draw; the
score sits under the middle board, and at the end the boards dim and the
verdict comes up in the middle. The
light-cycles are four small robots that turn towards open space; one that
hits a trail crashes, its trail fades and it comes back. The closedown runs
colour bars, a test card, the caption, static, then the picture collapses to
a dot. Text in this scene is drawn by the shader from a font texture rather
than as dots, which keeps it quick on the Pi. All original designs in the
spirit of the era.

**Weather settings:** `style` (**N**: 0 live clouds, 1 infrared, 2 over
home, 3 aurora, 4 storms), `speed` (the drift and shimmer), `home_lat` and
`home_lon`, `zoom` (over home: 1, the default, shows the whole side of
the Earth round home; up to 6 zooms in), `drift` (clouds still or drifting with
the prevailing winds: trade winds west, westerlies east, a made-up motion
between the real three-hourly pictures), `cloud_op` (how thick the clouds
look), `coast` (coastlines glowing through the clouds), `lights` (city
lights), `storm_speed` (hours of storm per second), `storm_zoom` (1, the
default, shows the whole side of the Earth; up to 4 zooms in on the
storm), `text`, `bright`. It
has no number key: press `,` or `.` or pick it on the web remote. It needs
`tools/orbitfeed.py` running (see below) for its data.

* **Live clouds:** the Earth turning with real day and night and city
  lights, the latest clouds lit by the Sun, coastlines glowing through.
* **Infrared:** the same clouds coloured like a forecaster's enhanced
  infrared picture, brightest and highest tops in red and white. It is a
  look made from how bright the clouds are, not measured temperatures.
* **Over home:** zoomed in on home (`home_lat`, `home_lon`; orbitfeed reads
  them from `weather.cfg` and cuts out the sharper picture round them), with
  a marker on home and a faint 5-degree grid.
* **Aurora:** looking down on the North Pole, the aurora oval from NOAA's
  30-minute forecast, shimmering green (red where it is strongest), brighter
  on the night side.
* **Storms:** one famous storm after another (Gilbert, Andrew, Katrina,
  Haiyan, Patricia, Irma, Maria, Ophelia, Dorian, Lorenzo, Idai, Ian, Milton
  and more) from NOAA's IBTrACS archive. The view follows the storm as its
  track is drawn, coloured by strength from depression to category 5, with
  the name, category, wind and date.

**Sci-fi settings:** `style` (**N** steps through them: 0 hologram globe,
1 radar sweep, 2 hyperspace, 3 planet scan, 4 reactor core, 5 ship's
computer, with five rings of hex readouts out to the rim), `speed`, `scale`, `range` (radar range in km), `home_lat` and
`home_lon` (the radar's centre), `text` (readouts on or off), `bright`.
The radar shows the real satellites passing within range of home when
`orbitfeed` is running (north up, east right), and simulated contacts
otherwise, and says so. Hyperspace runs a 24-second loop: cruise, spool
up, jump, then drop back to normal space. The radar's home starts at
Greenwich, like orbit watch's; set yours here too.

## Live data for orbit watch

Orbit watch needs `tools/orbitfeed.py` running. It downloads satellite orbits
from CelesTrak twice a day and earthquakes from the USGS every five
minutes, works out every satellite's position every five seconds with the
SGP4 orbit model, and writes small files to `data/` that the scene reads.
It also fetches the data for the Weather scene: a map of the world's clouds
every three hours (2048 x 1024, plus a sharper 4096 x 2048 one, cut down
to the area round home, which needs Pillow), NOAA's aurora forecast every
half hour, and, about once a month, the famous storms picked out of NOAA's
best-track archive. That last one is a big download, so it runs in the
background and the storms style says so until it is done. Start the feed
with `--no-weather` to skip all of it.
If the network drops, it carries on with what it last downloaded. Without
the feed, or if its data is more than a minute old, the scene still shows
the Earth and says "NO LIVE DATA" across the equator. The radar in the
sci-fi scene uses the same data. Orbits come from
[CelesTrak](https://celestrak.org) (space stations, bright satellites, GPS
and weather satellites) and earthquakes of magnitude 2.5 and over from the
[USGS](https://earthquake.usgs.gov) feed for the past day.

```bash
cd ~/hemisphere
python3 -m venv venv
venv/bin/pip install -r tools/requirements.txt
venv/bin/python3 tools/orbitfeed.py           # try it: Ctrl+C to stop
sudo cp tools/orbitfeed.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now orbitfeed         # start now and at every boot
journalctl -u orbitfeed -f                    # watch what it is doing
```

The service file assumes the program lives in `/home/pi/hemisphere` and
runs as `pi`; edit `User=` and the paths to match your Pi. `--once` updates everything once
and exits, which is handy for checking the network.

## Web remote

A phone-friendly remote control page, so you don't need a keyboard or SSH:
scene buttons, the N, M and C keys, pause, faster and slower, save, and a
slider for every setting of the scene you're on. Numbered choices show
their names (`theme 1 · fire`). It also shows the frame rate and whether
the orbit feed is live.

**Playlist.** The remote can run the dome by itself, moving through a list
of scenes and styles. Build the list on the page: pick a scene and a style
and press **Add**, or **Add all its styles**, or **Add everything**; tick
items on and off, reorder them with the arrows, and give any item its own
time. Then choose the order (**each in turn**, **shuffle**, which shows each
item once per round, or **random**), how long each item stays up, how much
to vary that by at random, and how long to wait after you pick something by
hand before carrying on (0 to carry straight on). **Start playlist** and it
keeps going with no browser open, and picks up again after a reboot; **Back**
and **Skip** move along now. The list is saved in `playlist.json` in the
hemisphere folder as you edit it. It sets the style without saving it, so
press Save only if you want to keep what's showing.

```bash
cd ~/hemisphere
venv/bin/pip install -r tools/requirements.txt     # adds Flask
venv/bin/python3 tools/remote/app.py               # try it: Ctrl+C to stop
```

Then open `http://raspberrypi.local:5000` on your phone. To run it at boot:

```bash
sudo cp tools/remote/remote.service /etc/systemd/system/hemisphere-remote.service
sudo systemctl daemon-reload
sudo systemctl enable --now hemisphere-remote
```

**How it works.** Hemisphere listens for commands on `127.0.0.1:5005`
(UDP), which only programs on the Pi itself can reach. The Flask app serves
the page on port 5000 and passes commands through. Anyone on your home
network can open the page and there's no login, so keep port 5000 off the
internet. `DOME_CTL_PORT` changes the port (0 turns it off).

**API**, for scripts or Home Assistant:

| Call | Body | Does |
|------|------|------|
| `GET /api/state` | | Current scene, its settings with ranges and hints, fps, orbit feed age |
| `POST /api/scene` | `{"scene": 2}` | Switch scene, counting from 0 (-2 lens grid, -1 globe grid) |
| `POST /api/key` | `{"key": "n"}` | Press a key: letters, digits, `,` `.`, `space`, `up`, `down` |
| `POST /api/setting` | `{"name": "speed", "value": 2.5}` | Set a setting of the current scene or view |
| `POST /api/save` | | Save all settings files, like pressing S |
| `GET /api/catalog` | | Every scene, with the setting that picks its style (style, view or theme) and its range |
| `GET /api/playlist` | | The playlist and what it's showing now |
| `POST /api/playlist` | the whole playlist | Replace it (the format is described in `tools/remote/playlist.py`) |
| `POST /api/playlist/skip` | `{"step": 1}` | Next item now (`-1` goes back) |

For example: `curl -X POST -H 'Content-Type: application/json' -d '{"scene":6}' http://raspberrypi.local:5000/api/scene`
switches to the Moon.

## Running on its own

Once it's set up you don't need an SSH session open. Hemisphere starts with
the Pi's desktop (it needs the screen, which a background service can't
easily get), and the orbit feed and web remote run as services. After a
reboot all three come back by themselves. The examples use the `pi` user
and `/home/pi`; put your own in their place.

**The web remote and orbit feed as services.** The service files in
`tools/` assume `User=pi` and `/home/pi/hemisphere`. If yours differ, this
installs a copy with your user and folder filled in:

```bash
cd ~/hemisphere
for s in tools/remote/remote.service:hemisphere-remote tools/orbitfeed.service:orbitfeed; do
  sudo sh -c "sed 's#/home/pi/#$HOME/#g; s#User=pi#User=$USER#' ${s%%:*} > /etc/systemd/system/${s##*:}.service"
done
sudo systemctl daemon-reload
sudo systemctl enable --now hemisphere-remote orbitfeed
systemctl status hemisphere-remote orbitfeed --no-pager
```

**Hemisphere with the desktop.** Raspberry Pi OS's labwc desktop runs the
commands in `~/.config/labwc/autostart` when it logs in. Check what's
already there, then add Hemisphere:

```bash
cat ~/.config/labwc/autostart 2>/dev/null
mkdir -p ~/.config/labwc
echo "sh -c \"sleep 5; cd $HOME/hemisphere && DISPLAY=:0 ./hemisphere > /tmp/hemisphere.log 2>&1\" &" >> ~/.config/labwc/autostart
```

The Pi needs to log in to the desktop by itself at boot (`sudo raspi-config`,
System Options, Boot / Auto Login, Desktop Autologin).

**Start it now without rebooting**, so it keeps running after you close
the SSH session:

```bash
cd ~/hemisphere && DISPLAY=:0 nohup ./hemisphere > /tmp/hemisphere.log 2>&1 &
```

**Living with it:**

* Keys typed over SSH only reach Hemisphere when you started it in that
  SSH session. Running on its own, use the web remote instead. For a long
  keyboard session, such as calibrating, run `pkill hemisphere` and start
  it in the foreground in SSH as before.
* `cat /tmp/hemisphere.log` shows what Hemisphere printed, if something
  seems wrong.
* To stop it starting at boot, delete its line from
  `~/.config/labwc/autostart`; for the services, `sudo systemctl disable
  --now hemisphere-remote orbitfeed`.
* If another program on the Pi already uses port 5000 (`sudo ss -tlnp`
  shows it), run the remote on another port: add
  `Environment=REMOTE_PORT=5001` under `[Service]` in its service file,
  then `sudo systemctl daemon-reload` and restart it.

## Windows PC

The same code runs on a Windows 10 or 11 PC, scenes and all. Windows has no
OpenGL ES library, but NVIDIA, Intel and AMD drivers offer the same
functions, so the core fetches them from the driver at start-up
(`dome/glload.c`) and nothing above it changes. If a driver will not give
an OpenGL ES context, Hemisphere falls back to desktop OpenGL 2.1 and
adjusts the shaders to suit; the start-up line says `(desktop fallback)`
when that happens. It needs a real graphics driver, not the "Microsoft
Basic Display Adapter".

A PC has far more graphics power than a Pi 3, so turn things up: `res`
1024 or more for a sharper dome, and Julia's `iter` back to 128. Settings
files are per machine, so this doesn't touch the Pi's.

### Build

Install [MSYS2](https://www.msys2.org), open the **MSYS2 UCRT64** terminal
from the Start menu, then:

```bash
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
cd /d/portfolio/hemisphere          # your copy of the repo
make
./hemisphere.exe
```

`make` builds `hemisphere.exe` and copies `SDL2.dll` next to it. Those two
files are all it needs, so the folder can be copied to another PC without
MSYS2. Press **ESC** in the dome window, or Ctrl+C in the terminal, to quit.

For the orbit feed and web remote, make the Python venv from PowerShell in
the repo folder:

```powershell
py -m venv venv
venv\Scripts\pip install -r tools\requirements.txt
```

### The WorldEye as a second screen

Plug the WorldEye into a spare HDMI output and set Windows to **Extend
these displays** (Settings, System, Display). Hemisphere opens full screen
on display 0, the main one. Press **V** until it moves onto the WorldEye,
then **S** to save; the choice is the `display` view setting, also on the
web remote. Clicking on your other screen doesn't minimise the dome.

Check the calibration on the lens grid (**G**) the first time. The
calibration is in fractions of the screen, so it carries over from the Pi
if the WorldEye gets the same resolution, but a different HDMI mode moves
things slightly.

With the dome on a second screen, keys only reach Hemisphere when its
window has focus, so the [web remote](#web-remote) is the easy way to
drive it. The terminal key reader the Pi uses over SSH isn't on Windows.

### Running on its own

`tools\windows\start-hemisphere.ps1` is the Windows counterpart of the
Pi's services: it starts Hemisphere with its window hidden, plus the orbit
feed and web remote from the venv, restarts any that stop, and writes logs
to `logs\`. ESC on the dome stops the lot. To start it at sign-in, run once
from an administrator PowerShell in the repo folder:

```powershell
powershell -ExecutionPolicy Bypass -File tools\windows\install-autostart.ps1
```

That adds a scheduled task, **Hemisphere**, and a firewall rule letting
phones on your home network reach the web remote on port 5000 (Private
networks only; `-Port 5050` if you changed `REMOTE_PORT`, `-Remove` to undo
it). Then open `http://<pc-name>:5000` on your phone.

For a PC that comes back by itself after a power cut:

* **Sign in automatically.** The dome needs a signed-in desktop. Microsoft's
  free [Autologon](https://learn.microsoft.com/sysinternals/downloads/autologon)
  tool sets this up and stores the password encrypted.
* **Never sleep or blank the screen.** Settings, System, Power: set screen
  and sleep to **Never**, and turn off any screen saver.

### Remote Desktop

Don't start Hemisphere from a Remote Desktop session. A program started
under RDP draws to the remote screen, not the PC's HDMI outputs, so the
dome stays dark. Closing RDP the normal way also locks the session, which
blanks the dome until someone signs in at the PC.

If you do RDP in for admin, leave by running `tools\windows\rdp-to-console.bat`
as administrator instead of closing the window. It hands your session back
to the PC's own screens, still signed in, and the dome carries on. Remote
tools that share the real screen, such as VNC or Chrome Remote Desktop,
don't have this problem.

## Controls

**On the Pi's keyboard:**

| Key | Action |
|-----|--------|
| 1 to 9, 0 | Death Star, planet, plasma, Julia, kaleidoscope, Game of Life, Moon, orbit watch, groovy, sci-fi (D also gives the Death Star) |
| , and . | Previous / next scene |
| G / B | Lens grid / globe grid (adjust calibration) |
| TAB / Shift+TAB | Next / previous setting in the current group |
| LEFT / RIGHT | Adjust the selected setting (hold Shift for fine steps) |
| O | Draw the globe grid over the current scene |
| V | Move to the next screen (a PC with the WorldEye as a second display) |
| X | Mirror left/right |
| T | Spin the current grid on/off |
| S or F5 | Save all settings files |
| L | Reload all settings files |
| R | Reset the current group to defaults |
| P / H | Print all settings / the key list |
| SPACE | Pause rotation |
| UP / DOWN | Faster / slower rotation |
| N | Next colour theme (plasma, Julia, kaleidoscope, Life), style (Groovy, Sci-fi, Computers, Hypnotic, Sports balls, 8-bit retro, Retro screen, Weather), map (planet) or view (orbit watch). Death Star: thinner lines |
| M | Hypnotic: next colours. Computers: phosphor colour. Julia: Julia / Mandelbrot. Kaleidoscope: mirror style. Life: next rules. Planet: previous map. Death Star: thicker lines |
| C | Life: reseed. Death Star: cycle phosphor colour |
| I / K | Death Star: dish shallower / deeper |
| Z | Death Star: replay the intro zoom |
| ESC | Quit |

**In the SSH window:** UP/DOWN pick a setting, LEFT/RIGHT adjust it, `[`
and `]` make fine adjustments, `+`/`-` change rotation speed and `q` quits.
Letter and number keys, and `,` and `.`, work as above.

## The calibration grids

**Lens grid (G).** Rings round the centre of the dome every 15 degrees:
white at 30 and 60, yellow at 90 (the rim). Twelve spokes, with the **up**
spoke yellow and the **right** spoke red, and a small cross at the centre.

**Globe grid (B).** Lines of latitude and longitude, as a globe seen from
the front: yellow equator and front meridian, red back meridian.

**What "correct" looks like.** On the lens grid the rings should be round,
centred on the dome, and evenly spaced along the surface. Measure with a
strip of paper from the centre: centre to 30, 30 to 60 and 60 to the rim
should all match.

## Calibration workflow

Press **G** and TAB through the settings, in this order:

1. **centre_x, centre_y.** Put the centre cross in the middle of the dome.
2. **aspect.** Make the rings round.
3. **radius.** Put the yellow 90 degree ring on the rim of the dome.
4. **offset.** If the 30 and 60 rings are crowded towards the centre,
   increase it; if crowded towards the rim, decrease it. The rim won't move.
5. **ring30, ring60.** Nudge either ring if it's still a little off.
6. **turn.** Rotate until the yellow spoke points straight up. If the red
   spoke is on the left, press **X** to mirror.
7. **cutoff.** Raise it until nothing is drawn past the rim. 90 is right
   for a true half globe.
8. **T** spins the lens grid: the rings should stay still.
9. **B** for the globe grid: check the equator is level.
10. **S** to save, then **1** for the Death Star. Set `pitch` first, to put
    the pole at the top of the globe (pause with SPACE to judge it).

## Writing a new scene

Copy `scenes/plasma.c`, which is about 50 lines. `julia.c` shows a scene
that animates its own uniforms in `update()`. A scene is:

1. A struct of settings and a `Param` table describing them (name, step,
   range, a hint). The core handles TAB, adjusting, printing, saving and
   loading.
2. Optionally, a `surface()` function in GLSL, built with
   `dome_surface_program()` and drawn between `dome_surface_begin()` and
   `dome_surface_end()`. Set your own uniforms in between; texture units 1
   and up are yours. `dome_surface_program_themed()` adds ready-made noise
   helpers and the shared colour themes (`theme(id, x)`); see `plasma.c`.
3. Optionally, dots: `dome_points_init`, `dome_points_line`,
   `dome_points_upload`, then `dome_draw_points` each frame.
4. Optionally, text: `dome_text_block` for lines across the equator (it
   returns the size of the block, for a band behind it), `dome_text_line`
   for one line, or `dome_text_arc` round the rim. Draw text with
   `dome_view_rot()` so it sits the right way up and doesn't spin.
5. A `Scene` struct naming its settings file and callbacks.

Then add it to the list in `main.c` and to `SRC` in the `Makefile`. Ten
scenes fit on the number keys; up to 16 can be reached with `,` and `.`. A
different project, such as a planet projector on its own, is just a new
`main.c` listing its own scenes. `dome/dome.h` documents every call.

**Keep it quick on the Pi 3.** `surface()` runs for every pixel, every
frame. Layers of noise are the expensive part: the first liquid light and
reactor core stuttered for that reason, and were rewritten from a handful
of `sin()` waves, which look much the same at a fraction of the cost.
Lowering `res` is the quick fix for any scene.

## Testing helpers

These environment variables are for testing and previewing:

| Variable | Effect |
|----------|--------|
| `DOME_FPS=1` | Print the frame rate every ten seconds |
| `DOME_MODE=n` | Start on a scene, counting from 0 (so `DOME_MODE=6` is the Moon, scene 7); -2 is the lens grid, -1 the globe grid |
| `DOME_DAYS=n` | Move the Moon scene's clock n days on (or back, if negative) |
| `DOME_OVERLAY=1` | Start with the globe grid drawn over the scene (as if **O** had been pressed) |
| `DOME_SHOT=n` | Save frame n to `shot.ppm` and quit |

## Debugging notes

* **Surfaces and the lens.** Running the lens formula backwards in the
  shader for every pixel would cost several trig functions per pixel and
  wouldn't agree exactly with the ring corrections. Baking it into a lookup
  texture on the CPU (a fraction of a second, only when calibration changes)
  makes surfaces cheap and exact.
* **Settings file location.** Settings files are kept beside the program
  (via `realpath()`), so launching from another folder doesn't lose them.
* **F5 on compact keyboards.** Some small keyboards send `AC Search`
  instead of F5. Both are handled, and S saves as well.
* **Dish depth.** Recessed dots are placed along the line of sight from a
  viewer in front of the globe, which gives real parallax. Near the edge of
  the dome the near rim of the bowl hides the dots behind it.
* **Grid squares vanishing.** A straight chord between two points on the
  globe dips inside it, so wide grid squares used to look recessed and got
  hidden. Every line dot is pushed back out to the surface.
* **Bright poles.** All the meridians meet at the poles, so they are
  thinned out above 60 and 75 degrees.
* **Calibrating from a PC.** The program window only sees the Pi's own
  keyboard, so keys typed in the SSH window are read separately and fed in
  as the same key presses.
* **Slow scenes.** Liquid light, the reactor core, the hologram globe and
  the planet scan were too slow on the Pi 3 because they stacked noise for
  every pixel. All four now use sine waves. Julia's `iter` now starts at
  32 rather than 64, and the Death Star's motion trail is one faint copy
  rather than two. The newer scenes give each style its own small shader,
  so only the one on screen costs anything. If another style stutters, lower `res`, or rebuild its pattern
  from sine waves the same way.
* **The original two-pass design.** Version one filmed the model with a
  normal camera and fisheye-warped the picture. A camera only sees the
  front of a ball, so the warp put a small flat picture in the middle of the
  dome. Mapping every point straight onto the dome fixed it, and the
  surface lookup texture uses the same idea for colour.

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

Built with AI-assisted tooling (Claude Code).
