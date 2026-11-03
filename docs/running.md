# Building and running on a Pi

Part of [Hemisphere](../README.md). Building on the Pi, copying code to it, adding planet maps, and setting it to run by itself at boot. For a Windows PC, see [Windows PC](windows.md).

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

then on the Pi: `cd ~/hemisphere && make && tools/start-hemisphere.sh --detach`.

The first time, copy the whole folder instead (`scp -r hemisphere
pi@raspberrypi.local:~/`). From Git Bash, WSL or Linux, `./deploy.sh` does
all of this in one go: it copies the files (only copying settings files the
Pi doesn't already have), builds, and starts Hemisphere again in the
background, the same way it starts at boot (see [Running on its own](#running-on-its-own)), so
it keeps running when the SSH session ends. Tell it where your Pi is:
`PI=pi@raspberrypi.local DEST=/home/pi/hemisphere ./deploy.sh`. If the Pi lost power
part way through a build, `deploy.sh` also clears out the empty files that
leaves behind, which `make` would otherwise take as up to date.

### Adjusting settings

To adjust settings, stop the background copy first
(`~/hemisphere/tools/start-hemisphere.sh --stop`), then run
`cd ~/hemisphere && DISPLAY=:0 ./hemisphere` in the foreground in your SSH
session. It prints each setting as you change it, and you can press keys
right there in the SSH window. Press **S** to save and **q** to quit.
Quitting does not save. It starts on the Death Star; press **G** for the
calibration grids, and **H** for a list of keys. When you're done,
`tools/start-hemisphere.sh --detach` sets it running in the background
again.

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
commands in `~/.config/labwc/autostart` when it logs in. Hemisphere starts
from there through `tools/start-hemisphere.sh`, which waits until the
desktop's display is ready, restarts Hemisphere if it ever stops, and logs
to `~/hemisphere/hemisphere.log` (the run before is kept as
`hemisphere.log.1`). No sudo is needed. Check what's already there, take
out any older Hemisphere line, then add it:

```bash
cat ~/.config/labwc/autostart 2>/dev/null
mkdir -p ~/.config/labwc
echo "bash $HOME/hemisphere/tools/start-hemisphere.sh &" >> ~/.config/labwc/autostart
```

The Pi needs to log in to the desktop by itself at boot (`sudo raspi-config`,
System Options, Boot / Auto Login, Desktop Autologin).

**Start it now without rebooting**, so it keeps running after you close
the SSH session (`deploy.sh` does this for you after each build):

```bash
~/hemisphere/tools/start-hemisphere.sh --detach
```

**Living with it:**

* Keys typed over SSH only reach Hemisphere when you started it in that
  SSH session. Running on its own, use the web remote instead. For a long
  keyboard session, such as calibrating, run
  `~/hemisphere/tools/start-hemisphere.sh --stop` (a plain `pkill
  hemisphere` only makes it restart) and start it in the foreground in SSH
  as before; `--detach` puts it back in the background afterwards.
* `cat ~/hemisphere/hemisphere.log` shows what Hemisphere printed, if
  something seems wrong. If it says `./hemisphere is missing or empty`, a
  build was cut short: run `make` again, or `deploy.sh`.
* If the Pi resets or freezes during a build, check its power supply:
  `vcgencmd get_throttled` showing anything other than `throttled=0x0`
  means it has run short of power. Use a 5.1 V 2.5 A supply.
* To stop it starting at boot, delete its line from
  `~/.config/labwc/autostart`; for the services, `sudo systemctl disable
  --now hemisphere-remote orbitfeed`.
* If another program on the Pi already uses port 5000 (`sudo ss -tlnp`
  shows it), run the remote on another port: add
  `Environment=REMOTE_PORT=5001` under `[Service]` in its service file,
  then `sudo systemctl daemon-reload` and restart it.
