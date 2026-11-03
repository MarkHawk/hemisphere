# Settings

Part of [Hemisphere](../README.md). Every scene's settings, and the files they live in.

Jump to: [the scenes in full](#the-scenes-in-full), [files and keys](#settings-files-and-keys), [view](#view-settings), [Death Star](#death-star), [Planet](#planet), [colour themes](#colour-themes), [Plasma](#plasma), [Julia](#julia), [Kaleidoscope](#kaleidoscope), [Game of Life](#game-of-life), [Moon](#moon), [Orbit watch](#orbit-watch), [Groovy](#groovy), [Computers](#computers), [Hypnotic](#hypnotic), [Sports balls](#sports-balls), [8-bit retro](#8-bit-retro), [Retro screen](#retro-screen), [Eyes](#eyes), [Music](#music), [Now playing](#now-playing), [Weather](#weather), [Sci-fi](#sci-fi).

## The scenes in full

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
| , and . | 8-bit retro | Chunky-pixel effects: digital rain, copper bars with a sine scroller, a rotozoomer, a maze chase, Rim defender, Asteroids on a globe, 16-colour plasma, tape loading (a random picture each time, or your own photos), pixel fire. The three games are full games that play themselves across the whole dome |
| , and . | Retro screen | 80s film and TV screens: a defence command globe with missile tracks, noughts and crosses played to a draw on 23 boards, a cyborg's visor, a ship's computer terminal, light-cycles on a neon grid, and a TV closedown |
| , and . | Weather | Today's clouds over the turning Earth (still or drifting), an infrared-style picture, a sharp view zoomed in over home, tonight's aurora forecast round the pole, and famous hurricanes and typhoons replayed from the record |
| , and . | Eyes | One big eye filling the dome, looking round the room, blinking, its pupil opening and closing: brown, blue, green and hazel human eyes, a cat, an owl, a reptile, and an original fire eye of glowing embers in an obsidian eyeball |
| , and . | Music | Built for sound from your WLED lights (see [Sound from WLED](sound.md#sound-from-wled)): 21 styles: spectra, rings, ripples, a starfield, aurora, oscilloscope and Lissajous figures, a wire tunnel and a spectrum landscape, particle bursts, a galaxy, rain, fireworks, an 8-bit analyser, plasma, a kaleidoscope, liquid light and a ring of fire. With no music each style idles calmly |
| , and . | Now playing | What your HEOS amp is playing (see [Now playing](sound.md#now-playing-from-a-heos-amp)): the cover filling the dome, a turning record or CD, the track as a day with a moon, today's covers as an atlas of countries, the cover in dots dancing to the music, or a status display with the time, volume and up next. Moves lightly with your WLED lights |

## Settings files and keys

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
| `eyes.cfg` | Eyes | Eyes (, and .) | Style, speed, how far it looks, blinks, lids, pupil size, shine |
| `weather.cfg` | Weather | Weather (, and .) | Style, home, zoom, clouds, coasts, storm speed |
| `music.cfg` | Music | Music (, and .) | Style, sound on or off, how strongly it reacts, speed, colours |
| `nowplaying.cfg` | Now playing | Now playing (, and .) | Style, sound on or off, how strongly it reacts, speed, colours, rim text, dots |

In a scene, TAB steps through the six view settings first (`pitch`,
`tilt`, `wobble`, `rev_secs`, `res`, `view_dist`) and then that scene's
own, so press TAB six times to reach a scene's first setting. Calibration
is only reachable from the grids, so you can't knock it out while styling.
**S** saves all the files. **R** resets the group you are in: on a grid,
the calibration; in a scene, the view settings and that scene's own. **L**
reloads what you last saved. Settings a file has that the program no
longer uses are ignored, and the next save drops them.

## Coming from worldeye

If `calibration.cfg` or `deathstar.cfg` is
missing, the old `worldeye.cfg` and `look.cfg` are read instead, including
the view settings that used to live in `look.cfg`. The first save writes
the new files, and the old ones can then be deleted. To move an existing
install on the Pi, copy the two old files into `/home/pi/hemisphere`.

## View settings

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
`tilt` but never spin. The lava lamp, spirograph, radar, hyperspace and ship's computer are drawn square to the dome, ignoring the view settings.

## Death Star

The scene opens with the wireframe zooming out from a
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
| `audio` | 1 makes the wireframe bend to music from your WLED lights (see [Sound from WLED](sound.md#sound-from-wled)); 0, the default, leaves it as it is |
| `warp` | How far it bends, 0 to 1 (0.4 to start). The bass swells the whole sphere out and back, the snare body sends ripples out from the dish, each beat rolls a wave down from the pole, and each line of latitude wobbles with its own band. Only the lines bend: in the hull styles they move a little over the still hull |

The hull's panelling, panel size, seams, dish position and pole dimming are
fixed in the code rather than adjustable settings.

## Planet

Settings: `bright`, `sun` (0 lit all over, 1 sharp night side),
`night` (how much of the night side shows), `atmos` (rim glow), `bands`
(gas giant only), `lights` (city lights on the night side).

## Colour themes

Plasma, Julia, the kaleidoscope and Life share eight
colour themes, and **N** steps through them: rainbow, fire, ice, neon,
aurora, gold, amber and jewel (stained-glass colours with dark leading). In
Life the first theme is its own classic colouring instead of rainbow. The
`hue` setting, where a scene has one, slides the colours along within a
theme.

## Plasma

Settings: `theme`, `speed`, `scale`, `bands`, `sat`, `bright`.

## Julia

Settings: `theme`, `set` (0 Julia, 1 Mandelbrot; **M** switches),
`edge` (under 1 gives solid shapes, over 1 lacy dust), `speed`, `iter`
(detail; more is slower, 32 to start), `zoom`, `hue`, `bands` (how fast the colours
cycle), `glow` (brightness inside the set). Infinity sits at the north
pole, so the top of the globe is the dark outside of the set. The
Mandelbrot set is the map of every Julia set: it stays still while the
globe turns it.

## Kaleidoscope

Settings: `theme`, `style` (0 cube mirrors, 1 wedges;
**M** switches), `mirrors` (wedges, style 1), `fold` (the fractal's shape:
tiny changes give very different patterns, so step it with Shift for fine
steps), `depth`, `speed`, `hue`, `sat`, `bright`.

## Game of Life

Settings: `theme`, `rule` (**M** steps through them, and
reseeds: 0 Conway's original B3/S23; 1 HighLife B36/S23, which adds
self-copying replicators; 2 Day & Night B3678/S34678, big blobs that grow
and shrink), `cells` (cells along each cube face edge; the globe has six
faces, so 48 gives 13,824 cells. Changing it reseeds), `speed` (generations
per second), `density` (how full a reseed is), `gap` (space between tiles),
`trail` (how long dead cells leave a ghost), `glow`, `hue`. Newborn cells
are the brightest, cooling as they age. **C** reseeds the whole globe. The
globe is laid out as an inflated cube, so at its eight corners three faces
meet and a cell has seven neighbours instead of eight.

## Moon

Settings: `shine` (earthshine on the dark side), `rock` (a
slow, gentle rocking, like the Moon's real libration), `text` (0 hides the
text), `band` (how dark the band behind the text is), `bright`. The Moon
fills the whole dome, facing you, with north at the top of the globe as set
by the view settings (`pitch`, `tilt`). Uses any map with "moon" in its name
from `textures/`. Four lines across the equator give the phase and how much
is lit, the Moon's age, and the next full and new moon in local time. They
are worked out from the Pi's clock (Meeus, "Astronomical Algorithms", good
to a few minutes) and update every minute. To preview another night, start
it with `DOME_DAYS=5 ./hemisphere` for five days from now.

## Orbit watch

Settings: `view` (0 turning, 1 following the ISS, 2 held
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

## Groovy

Settings: `style` (**N** steps through them: 0 op art, 1
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

## Computers

Settings: `style` (**N**: 0 front panel, 1 oscilloscope, 2
code listing, 3 circuit board, 4 core memory, 5 packet network, 6
blinkenlights), `speed`, `phosphor` (**M**: one screen colour for every
style, 0 green, 1 amber, 2 white, or 3 full colour, the default), `text` (labels
and readouts), `bright`. It has no number key: press `,` or `.` to reach
it, or pick it on the web remote. The front panel's switches are toggled
one at a time to load an address and deposit a short made-up program, word
by word, which then runs with the lamps flickering before it halts and
starts again. Core memory, blinkenlights and the packet network turn with
the globe; the rest face you.

## Hypnotic

Settings: `style` (**N**: 0 tesseract, 1 quasicrystal, 2 moire,
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

## Sports balls

Settings: `style` (**N**: 0 football, 1 basketball, 2
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

## 8-bit retro

Settings: `style` (**N**: 0 digital rain, 1 copper bars, 2
rotozoomer, 3 maze chase, 4 Rim defender, 5 Asteroids, 6 16-colour
plasma, 7 tape loading, 8 pixel fire), `speed`, `scale` (pixel size),
`bright`, `grid` (show the gaps between pixels). The three games are
full games, each played by a small robot in C, with lives, a score and high
score, levels that get harder, and GAME OVER then a fresh game.

* **Tape loading:** a home-computer screen loading off cassette, done the
  way the real thing worked. The border shows the red and cyan pilot tone,
  then the blue and yellow data; `Program: NAME` appears; then the picture
  arrives in black on white in the real screen-memory order (in thirds, one
  pixel line of every character row at a time), and the colours flood in
  last. Each 8x8 cell has only two colours, so the colours clash at the
  cell edges just as they did. Every load is a different picture, shuffled
  so none repeats until all have been shown: eleven made-up ones built in
  (lighthouse, castle, night city, pyramids, mountains, island, galleon,
  spaceman, explorer, wizard, racer), plus any photos you put in a `tapes`
  folder next to the program (`.jpg`, `.png`, `.bmp` or `.gif`; the file
  name becomes the tape name; about 1024 pixels across is plenty, as big
  files take a moment to read on a Pi 3). Photos are converted when they
  load, so nothing needs preparing. The `tapes` folder is not committed.

* **Rotozoomer:** the demo-scene classic. A made-up pixel tile (a framed
  diamond gem on a chequer) turns and zooms in and out while its colours
  cycle through the 16-colour palette. One small shader, no dots.
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

## Retro screen

Settings: `style` (**N**: 0 defence command, 1 noughts
and crosses, 2 cyborg HUD, 3 ship's terminal, 4 light grid, 5
closedown), `speed`, `text` (the readouts round
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

## Eyes

Settings: `style` (**N**: 0 brown, 1 blue, 2 green, 3 hazel, 4 cat,
5 owl, 6 reptile, 7 fire eye), `speed` (how lively it is), `look` (how far
it glances round; 0 stares straight at you), `blink` (0 or 1), `lids` (0 the
bare eyeball, 1 with its lids: skin, fur, feathers, scales or rock),
`pupil` (smaller or wider than it rests), `shine` (the wet sheen and the
window reflected in the cornea), `bright`. For realism on a Pi 3, the
detail is worked out once when the style changes (about a fifth of a second
on a PC): the iris fibres, crypts and the ring round the pupil, the veins
in the white, and the lids go into three textures, and the shader only
looks them up and adds the light. The eye flicks to a new spot and holds
there, blinks every few seconds (sometimes twice), and its pupil drifts
wider and narrower. The fire eye is an original design: a round pupil in an
iris of streaming embers, an obsidian eyeball with lava veins, dark rock
with glowing seams for lids, heat shimmer and rising sparks.

## Music

Settings: `style` (**N**, 21 styles, listed below), `audio` (1,
the default here, reacts to music from your WLED lights; 0 shows only the
calm idle), `react` (how strongly it responds), `speed`, `theme` (**M**:
the eight shared colour themes, then 8 album: the colours of what your
HEOS amp is playing, see [Now playing](sound.md#now-playing-from-a-heos-amp)), `bright`. Every style has a slow idle of
its own for when no music is playing, and the change between the two is
eased over about a second. Nothing flashes: beats swell, move and light
small areas, and sound changes the overall brightness by 15% at most.

| Style | What it shows |
|-------|---------------|
| 0 spectrum ring | 16 bars standing in from the rim, bass at the bottom and treble at the top; loud bass reaches nearly to the middle |
| 1 pulse globe | A glowing ball in the middle swelling with the bass and the beat, its halo following the treble |
| 2 ripples | Each snare hit sends a ring out from the middle |
| 3 starfield | Stars streaming out from the middle, faster with the bass, stretching into streaks (the stars are soft fixed tints; the theme colours the glow behind them) |
| 4 aurora | Curtains of light across the top, swaying in time, further when the snare is loud |
| 5 radial bars | 64 thin bars standing out from the middle, their tips glowing a little more on the beat |
| 6 oscilloscope | A trace round the rim in which every band drives its own ripple, bass long and slow, treble fine and fast, with fading after-images (WLED sends no waveform, so the trace is made from the levels) |
| 7 Lissajous | A turning figure, a quarter turn a beat, sized by the bass, morphing to a new shape every 20 seconds |
| 8 wire tunnel | Rings flying out of the middle on the bass, bending with it and bulging on the beat |
| 9 terrain | A wireframe landscape below the horizon, each row a spectrum as it played, scrolling towards you |
| 10 bursts | Each beat sends a ring of particles out from the middle |
| 11 dot globe | A sphere of dots turning with the globe, breathing with the bass, warming with the treble |
| 12 galaxy | Two spiral arms spinning with the tempo, the core glowing with the bass |
| 13 rain | Streaks falling faster and thicker with the treble, splashing at the bottom rim |
| 14 fireworks | Beats launch shells that burst into drifting sparks |
| 15 8-bit spectrum | Chunky LED columns with peak caps that drop slowly |
| 16 bass plasma | A flowing colour field, faster with the bass |
| 17 kaleidoscope | Six mirrored folds turning slowly, the zoom breathing on the beat |
| 18 colour tunnel | Flying down a tunnel of colour on the bass |
| 19 liquid light | Five soft blobs of coloured oil, mixing where they meet, moving with the snare body |
| 20 fire ring | Flames rising from the rim, each part burning with its own band, flaring with the bass and throwing sparks on beats; embers when idle (fire keeps its own colours) |

Every style holds 60 frames a second on a Pi 3. Each has its own small
shader, the costly maths is baked into textures or worked out once a
frame on the CPU, and lines and particles are dots.

## Now playing

Settings: `style` (**N**, 6 styles, listed below; the
now-playing tool picks one to suit the source), `audio` (1, moves lightly
with the music from your WLED lights), `react`, `speed`, `theme` (**M**;
8, album, the cover's own colours, is the default), `bright`, `text` (1
shows the title and artist round the rim) and `dots` (style 4: 96 or 128
across). It needs `tools/nowplaying` running (see [Now playing from a
HEOS amp](sound.md#now-playing-from-a-heos-amp)); without it every style shows
its idle look in the current colours. Between tracks the globe turns
half a revolution: the new cover comes in from behind the horizon and the
old one goes round the back.

| Style | What it shows |
|-------|---------------|
| 0 album globe | The cover filling the whole dome, centre to rim, swaying a little and breathing with the bass; the title round the top of the rim, artist and album (or station) round the bottom |
| 1 record planet | The globe as a record facing you: grooves, a sheen that stays with the light and shimmers with the mids, the cover as the label turning at 33 1/3 rpm (stripes in your colours on phono, which sends no cover), a tonearm moving in through the track; it spins down on pause. For a CD, a silver disc with a rainbow sheen and a label in your colours. Labels swell slightly on the beat |
| 2 track as a day | The cover lit by a sun crossing it through the track, dawn to dusk; lights across the night side twinkle with the treble, and a moon orbits once per track, glowing on the beat |
| 3 album atlas | Today's covers tiled over the globe like countries; it turns to bring each new one to the front, the coastlines rippling with the bass |
| 4 art into particles | The cover as 128 x 128 dots, each moving with its band of the music and settling back into the picture between phrases |
| 5 track info | Title, artist and album, play state and time, a progress ring, up next, volume, shuffle and repeat, and the source with a small icon, with a thin spectrum ring round the rim |

All six hold 60 frames a second on a Pi 3 (measured at 58.5 to 60 with
the CPU throttled to 600 MHz by a weak power supply).

## Weather

Settings: `style` (**N**: 0 live clouds, 1 infrared, 2 over
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
`tools/orbitfeed.py` running for its data (see [live data](live-data.md)).

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

## Sci-fi

Settings: `style` (**N** steps through them: 0 hologram globe,
1 radar sweep, 2 hyperspace, 3 planet scan, 4 reactor core, 5 ship's
computer, with five rings of hex readouts out to the rim), `speed`, `scale`, `range` (radar range in km), `home_lat` and
`home_lon` (the radar's centre), `text` (readouts on or off), `bright`.
The radar shows the real satellites passing within range of home when
`orbitfeed` is running (north up, east right), and simulated contacts
otherwise, and says so. Hyperspace runs a 24-second loop: cruise, spool
up, jump, then drop back to normal space. The radar's home starts at
Greenwich, like orbit watch's; set yours here too.
