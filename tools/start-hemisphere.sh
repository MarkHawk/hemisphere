#!/usr/bin/env bash
# start-hemisphere.sh - run Hemisphere on the Pi's screen and keep it running.
# The desktop's autostart and deploy.sh both start it this way.
#   tools/start-hemisphere.sh            run here (autostart adds a trailing &)
#   tools/start-hemisphere.sh --detach   start in the background, safe to log out
#   tools/start-hemisphere.sh --stop     stop it (and stop it coming back)
# It waits for the desktop's display, restarts Hemisphere if it stops, and
# logs to hemisphere.log in the hemisphere folder (the last run's log is
# kept as hemisphere.log.1).
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"
SELF="$DIR/tools/start-hemisphere.sh"
LOG="$DIR/hemisphere.log"
PIDFILE="$DIR/.hemisphere.pid"

stop() {
    local pid
    pid=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$pid" ] && grep -qa start-hemisphere "/proc/$pid/cmdline" 2>/dev/null; then
        kill "$pid" 2>/dev/null
    else
        pid=
    fi
    rm -f "$PIDFILE"
    pkill -x hemisphere 2>/dev/null
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -x hemisphere >/dev/null || break
        sleep 0.5
    done
    pkill -9 -x hemisphere 2>/dev/null
    # let the old loop finish so a new one can take the lock
    while [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
    return 0
}

case "${1:-}" in
    --stop)   stop; exit 0 ;;
    --detach) stop
              setsid nohup bash "$SELF" </dev/null >/dev/null 2>&1 &
              exit 0 ;;
esac

# one copy only
exec 9>"$DIR/.hemisphere.lock"
flock -n 9 || exit 0
echo $$ >"$PIDFILE"

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"
exec >>"$LOG" 2>&1
say() { echo "$(date '+%F %T') start-hemisphere: $*"; }
# sleep in the background, so --stop can end the loop at once
nap() { sleep "$1" & wait $!; }

# the labwc desktop's X display (Xwayland), the same as starting it by hand
export HOME="${HOME:-$(getent passwd "$(id -u)" | cut -d: -f6)}"
export DISPLAY=:0
export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
unset WAYLAND_DISPLAY
cd "$DIR" || exit 1

# ready when the X socket is there and the desktop has written this boot's
# X cookie; without the cookie, SDL can't open the display
boot=$(date -d "$(uptime -s)" +%s)
ready() {
    [ -S /tmp/.X11-unix/X0 ] && [ -s "$XAUTHORITY" ] &&
        [ "$(stat -c %Y "$XAUTHORITY")" -ge "$boot" ] &&
        { ! command -v xset >/dev/null || xset q >/dev/null 2>&1; }
}
say "waiting for display $DISPLAY"
for _ in $(seq 120); do
    ready && break
    sleep 1
done
if ready; then say "display ready"; else say "display not ready after 120 s, trying anyway"; fi

fails=0
while :; do
    # an empty or missing program means a build was cut short: run make
    if [ ! -s ./hemisphere ]; then
        say "./hemisphere is missing or empty; run make (or deploy.sh) to rebuild it"
        nap 30
        continue
    fi
    say "starting"
    t0=$(date +%s)
    ./hemisphere 9>&-
    rc=$?
    say "stopped (exit code $rc)"
    # back off if it keeps failing straight away
    if [ $(( $(date +%s) - t0 )) -lt 30 ]; then fails=$((fails + 1)); else fails=0; fi
    if [ "$fails" -ge 5 ]; then nap 60; else nap 3; fi
done
