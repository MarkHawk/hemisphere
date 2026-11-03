# Controls and the web remote

Part of [Hemisphere](../README.md). The keys, and the web remote for driving it from a phone.

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
| N | Next colour theme (plasma, Julia, kaleidoscope, Life), style (Groovy, Sci-fi, Computers, Hypnotic, Sports balls, 8-bit retro, Retro screen, Weather, Music, Now playing), map (planet) or view (orbit watch). Death Star: thinner lines |
| M | Hypnotic, Music, Now playing: next colours. Computers: phosphor colour. Julia: Julia / Mandelbrot. Kaleidoscope: mirror style. Life: next rules. Planet: previous map. Death Star: thicker lines |
| C | Life: reseed. Death Star: cycle phosphor colour |
| I / K | Death Star: dish shallower / deeper |
| Z | Death Star: replay the intro zoom |
| ESC | Quit |

**In the SSH window:** UP/DOWN pick a setting, LEFT/RIGHT adjust it, `[`
and `]` make fine adjustments, `+`/`-` change rotation speed and `q` quits.
Letter and number keys, and `,` and `.`, work as above.

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
| `GET /api/state` | | Current scene, its settings with ranges and hints, fps, orbit feed age, sound levels |
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
