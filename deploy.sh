#!/usr/bin/env bash
# deploy.sh - copy Hemisphere to the Pi, build it and start it.
# Run from this folder (Git Bash, WSL or Linux):  ./deploy.sh
# Point it at your Pi:  PI=me@my-pi DEST=/home/me/hemisphere ./deploy.sh
set -euo pipefail
PI="${PI:-pi@raspberrypi.local}"
DEST="${DEST:-/home/pi/hemisphere}"

ssh "$PI" "mkdir -p $DEST/dome $DEST/scenes $DEST/third_party $DEST/textures $DEST/tools"
scp main.c Makefile README.md "$PI:$DEST/"
scp dome/*.c dome/*.h     "$PI:$DEST/dome/"
scp scenes/*.c            "$PI:$DEST/scenes/"
scp third_party/*.h       "$PI:$DEST/third_party/"
scp -r tools/*            "$PI:$DEST/tools/"
# maps, if any (2048x1024 or smaller)
ls textures/*.jpg textures/*.png >/dev/null 2>&1 && scp textures/*.jpg textures/*.png "$PI:$DEST/textures/" 2>/dev/null || true
# your own photos for the tape loading style, if any
if ls tapes/* >/dev/null 2>&1; then ssh "$PI" "mkdir -p $DEST/tapes"; scp tapes/* "$PI:$DEST/tapes/"; fi
# settings files are only copied if the Pi has none, so your saved
# calibration on the Pi is never overwritten
for f in calibration.cfg deathstar.cfg; do
    if [ -f "$f" ] && ! ssh "$PI" "test -f $DEST/$f"; then scp "$f" "$PI:$DEST/"; fi
done

# an empty object file or program is left behind if the Pi lost power
# mid-build, and make would take it as up to date: remove it first
ssh "$PI" "cd $DEST && find . dome scenes -maxdepth 1 -type f -size 0 \\( -name '*.o' -o -name hemisphere \\) -print -delete"
ssh "$PI" "cd $DEST && make"
# start it again the way the desktop's autostart does, detached so it
# keeps running after this SSH session ends; it logs to $DEST/hemisphere.log
ssh "$PI" "bash $DEST/tools/start-hemisphere.sh --detach"
echo "Built and started. Log on the Pi: $DEST/hemisphere.log"
