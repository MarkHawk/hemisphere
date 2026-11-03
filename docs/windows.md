# Windows PC

Part of [Hemisphere](../README.md).

**Windows support: built for it, not yet tested on real hardware.** The
code and scripts are written for Windows 10 and 11, but the MSYS2 build has
not yet been run with a WorldEye. This page describes how it is meant to
work. The Raspberry Pi 3 is the tested set-up (see
[running on a Pi](running.md)).

The same code is written to build for a Windows PC. Windows has no
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

## Build

Install [MSYS2](https://www.msys2.org), open the **MSYS2 UCRT64** terminal
from the Start menu, then:

```bash
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
cd /path/to/hemisphere              # your copy of the repo
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

## The WorldEye as a second screen

Plug the WorldEye into a spare HDMI output and set Windows to **Extend
these displays** (Settings, System, Display). Hemisphere is designed to
open full screen on display 0, the main one. Pressing **V** should move it
to the next screen until it reaches the WorldEye. Then **S** saves the
choice, which is the `display` view setting, also on the web remote.
Clicking on your other screen should not minimise the dome.

Check the calibration on the lens grid (**G**) the first time. The
calibration is in fractions of the screen, so it should carry over from
the Pi if the WorldEye gets the same resolution. A different HDMI mode is
likely to move things slightly.

With the dome on a second screen, keys should only reach Hemisphere when
its window has focus, so the [web remote](controls.md#web-remote) is
expected to be the easy way to drive it. The terminal key reader the Pi
uses over SSH isn't built for Windows.

## Running on its own

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

## Remote Desktop

Don't start Hemisphere from a Remote Desktop session. A program started
under RDP is expected to draw to the remote screen, not the PC's HDMI
outputs, so the dome would stay dark. Closing RDP the normal way should
also lock the session, which would blank the dome until someone signs in
at the PC.

If you do RDP in for admin, leave by running `tools\windows\rdp-to-console.bat`
as administrator instead of closing the window. It is designed to hand
your session back to the PC's own screens, still signed in, so the dome
carries on. Remote tools that share the real screen, such as VNC or Chrome
Remote Desktop, should not have this problem.
