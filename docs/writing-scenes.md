# Writing scenes

Part of [Hemisphere](../README.md). Where things are, how a scene is built, how to keep it quick on a Pi 3, and the switches for testing.

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
  dome/audio.h, audio.c    sound from WLED audio sync (self-contained)
  scenes/deathstar.c       1   scenes/life.c     6
  scenes/planet.c          2   scenes/moon.c     7
  scenes/plasma.c          3   scenes/orbit.c    8
  scenes/julia.c           4   scenes/groovy.c   9
  scenes/kaleido.c         5   scenes/scifi.c    0
  scenes/computers.c       the rest are on , and .:
  scenes/hypno.c, balls.c, retro.c, screen.c,
    weather.c, eyes.c, music.c, nowplaying.c
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
  tools/start-hemisphere.sh  starts Hemisphere with the desktop and keeps it running
  tools/requirements.txt   Python packages for the tools
  tools/audio/replay.py    replays a WLED audio capture, for testing
  tools/audio/billie.txt   one minute of real audio sync data
  tools/nowplaying/        what a HEOS amp is playing, for the dome (read only)
  tools/windows/           start-up scripts for a Windows PC
  deploy.sh                copy to the Pi and build (Git Bash, WSL or Linux)
  docs/                    these guides
  docs/screenshots/        photos for the gallery
  calibration.cfg          calibration (my WorldEye)
  deathstar.cfg            Death Star look
```

## Writing a new scene

Copy `scenes/plasma.c`, which is about 50 lines. `julia.c` shows a scene
that animates its own uniforms in `update()`. A scene is:

1. A struct of settings and a `Param` table describing them (name, step,
   range, a hint). The core handles TAB, adjusting, printing, saving and
   loading.
2. Optionally, a `surface()` function in GLSL, built with
   `dome_surface_program()` and drawn between `dome_surface_begin()` and
   `dome_surface_end()`. Set your own uniforms in between; texture units 1
   to 6 are yours (the core keeps the last one, 7, for the themes).
   `dome_surface_program_themed()` adds ready-made noise helpers and the
   shared colour themes (`theme(id, x)`); see `plasma.c`. The themes are a
   table drawn once at start, so every theme costs one texture read;
   `dome_theme_rgb()` gives the same colours on the CPU, for dots.
3. Optionally, dots: `dome_points_init`, `dome_points_line`,
   `dome_points_upload`, then `dome_draw_points` each frame.
4. Optionally, text: `dome_text_block` for lines across the equator (it
   returns the size of the block, for a band behind it), `dome_text_line`
   for one line, or `dome_text_arc` round the rim. Draw text with
   `dome_view_rot()` so it sits the right way up and doesn't spin.
5. A `Scene` struct naming its settings file and callbacks.

Then add it to the list in `main.c` and to `SRC` in the `Makefile`. Ten
scenes fit on the number keys; up to 24 can be reached with `,` and `.`
(`DOME_MAX_SCENES` in `dome/dome.h`). A
different project, such as a planet projector on its own, is just a new
`main.c` listing its own scenes. `dome/dome.h` documents every call.

To add a scene to this repo, also give it a row in the README's scene
table and a section in [settings](settings.md), and add a gallery photo
(see [the photo guide](screenshots/README.md)).

## Keep it quick on the Pi 3

`surface()` runs for every pixel, every frame. These rules come from
scenes that stuttered and were fixed:

* **Draw with the surface shader, and keep dots few.** Use dots for crisp
  lines and particles on top, not to fill areas.
* **Avoid layers of noise.** They are the expensive part. The first liquid
  light and reactor core stuttered for that reason. They were rewritten
  from a handful of `sin()` waves, which look much the same at a fraction
  of the cost. `groovy.c` (liquid light) and `scifi.c` (reactor core) show
  how.
* **Give each style its own small shader**, so only the one on screen
  costs anything.
* **Work costly maths out once.** Bake it into a texture when a setting
  changes, or work it out once a frame on the CPU. Eyes and Music do this.
* **Draw text in the shader** from a font texture, not as dots, when there
  is a lot of it. `screen.c` does this.
* **Check it.** `DOME_FPS=1 ./hemisphere` prints the frame rate every ten
  seconds. Lowering `res` is the quick fix for any scene.

## Making a scene react

To react to [sound from WLED](sound.md#sound-from-wled), add an `audio`
setting (0 off, 1 on), and in
`draw()`, between `dome_surface_begin` and `dome_surface_end`, call
`dome_audio_uniforms(prog, f, L.audio)`. Then declare the values you want
in the GLSL: `uniform float u_bass, u_body, u_mid, u_air, u_level, u_beat,
u_phase, u_live;`. `f->audio` has them all on the CPU too, with the tempo
and the 16 bands.

For speed that follows the music, keep your own clock and add to it each
frame (`t += f->dt * speed * (1.0f + bass)`), and pass that in. Multiplying
`u_time` by a changing speed makes it jump.

## Testing helpers

These environment variables are for testing and previewing:

| Variable | Effect |
|----------|--------|
| `DOME_FPS=1` | Print the frame rate every ten seconds |
| `DOME_MODE=n` | Start on a scene, counting from 0 (so `DOME_MODE=6` is the Moon, scene 7); -2 is the lens grid, -1 the globe grid |
| `DOME_DAYS=n` | Move the Moon scene's clock n days on (or back, if negative) |
| `DOME_OVERLAY=1` | Start with the globe grid drawn over the scene (as if **O** had been pressed) |
| `DOME_SHOT=n` | Save frame n to `shot.ppm` and quit |
| `DOME_AUDIO_DEBUG=1` | Print the sound levels, beat and tempo twice a second (see [Sound from WLED](sound.md#sound-from-wled)) |
| `DOME_THEME_CHECK=1` | At start, draw every colour theme both ways (its formula and the lookup table) and print the largest difference: about 0.008, a little more at jewel's sharp edges |
