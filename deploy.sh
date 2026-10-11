#!/usr/bin/env bash
# deploy.sh - copy Hemisphere to the Pi, build it and start it.
# Run from this folder (Git Bash, WSL or Linux):  ./deploy.sh
set -euo pipefail
PI="${PI:-pi@raspberrypi.local}"
DEST="${DEST:-/home/pi/hemisphere}"

ssh "$PI" "mkdir -p $DEST/dome $DEST/scenes $DEST/third_party $DEST/textures $DEST/tools"
scp main.c Makefile README.md "$PI:$DEST/"
scp dome/*.c dome/*.h     "$PI:$DEST/dome/"
scp scenes/*.c            "$PI:$DEST/scenes/"
scp third_party/*.h       "$PI:$DEST/third_party/"
scp tools/*               "$PI:$DEST/tools/"
# maps, if any (2048x1024 or smaller)
ls textures/*.jpg textures/*.png >/dev/null 2>&1 && scp textures/*.jpg textures/*.png "$PI:$DEST/textures/" 2>/dev/null || true
# settings files are only copied if the Pi has none, so your saved
# calibration on the Pi is never overwritten
for f in calibration.cfg deathstar.cfg; do
    if [ -f "$f" ] && ! ssh "$PI" "test -f $DEST/$f"; then scp "$f" "$PI:$DEST/"; fi
done

ssh "$PI" "cd $DEST && make && (pkill -x hemisphere || true)"
echo "Built. Start it on the Pi with:  cd $DEST && DISPLAY=:0 ./hemisphere"
