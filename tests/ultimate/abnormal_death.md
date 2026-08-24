```bash
#!/usr/bin/env bash
# Keep one ircserv alive on $PORT from a PRIVATE copy of the binary, and record
# every exit with its status so an abnormal death (signal/segv) is visible.
PORT=${1:-6767}
SCRATCH=/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad
BIN=$SCRATCH/ircserv.snapshot
EXITLOG=$SCRATCH/exits.log
cd "$SCRATCH" || exit 1
while :; do
    "$BIN" "$PORT" pass >> "$SCRATCH/srv$PORT.log" 2>&1
    rc=$?
    printf '%s exit=%d %s\n' "$(date +%H:%M:%S)" "$rc" \
        "$( [ $rc -gt 128 ] && echo "SIGNAL $((rc-128))" || echo "" )" >> "$EXITLOG"
    sleep 0.2
done


```