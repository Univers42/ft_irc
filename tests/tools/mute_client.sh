#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# mute_client.sh — a client that registers once and then never writes again.
#
# Shell replacement for the old mute_client.py. Two modes, for the two
# different things a "silent" client is used to test:
#
#   --silent  (default)  Register, then keep reading and timestamp everything
#                        that arrives, including the server closing the socket.
#                        This is the timeout/PING-disconnect test: the client
#                        is silent but healthy, and never shuts the socket
#                        down, so the server must decide on its own to drop it.
#
#   --freeze             Register, then SIGSTOP the reader so it stops
#                        draining the socket at all. Its receive buffer fills
#                        and stays full. This is the send-queue overflow test.
#
# Usage:
#   ./tools/mute_client.sh [--silent|--freeze] <host> <port> <password> <nick> [seconds]
#
# Defaults: 127.0.0.1 6667 pass mute1 400
# ---------------------------------------------------------------------------
MODE=silent
case "${1:-}" in
    --silent) MODE=silent; shift ;;
    --freeze) MODE=freeze; shift ;;
esac

HOST="${1:-127.0.0.1}"
PORT="${2:-6667}"
PASSWORD="${3:-pass}"
NICK="${4:-mute1}"
HOLD="${5:-400}"

command -v nc >/dev/null 2>&1 || { printf 'nc is required\n' >&2; exit 1; }

TMP="/tmp/mute_client_$$"
mkdir -p "$TMP" || exit 1
FIFO="$TMP/in"
mkfifo "$FIFO" || exit 1

nc "$HOST" "$PORT" < "$FIFO" > "$TMP/out" 2>/dev/null &
NCPID=$!
exec 3> "$FIFO"

START=$(date +%s)
stamp() {
    _now=$(date +%s)
    printf 't+%6ss  %s\n' "$((_now - START))" "$1"
}

cleanup() {
    kill -CONT "$NCPID" 2>/dev/null
    kill "$NCPID" 2>/dev/null
    exec 3>&- 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

# Exactly one burst of writes — the registration — and nothing ever again.
printf 'PASS %s\r\n' "$PASSWORD" >&3
printf 'NICK %s\r\n'  "$NICK"     >&3
printf 'USER %s 0 * :muted client\r\n' "$NICK" >&3
stamp "registration sent (nick=$NICK)"

if [ "$MODE" = "freeze" ]; then
    sleep 1
    kill -STOP "$NCPID" 2>/dev/null
    stamp "reader SIGSTOPped — socket no longer drained"
    stamp "nc pid $NCPID, holding ${HOLD}s (Ctrl-C to stop)"
    sleep "$HOLD"
    exit 0
fi

# --- silent mode: keep reading, report everything with a timestamp ---------
seen=0
i=0
limit=$((HOLD * 2))          # we poll twice a second
while [ "$i" -lt "$limit" ]; do
    total=$(wc -c < "$TMP/out" 2>/dev/null | tr -d ' ')
    [ -z "$total" ] && total=0
    if [ "$total" -gt "$seen" ]; then
        tail -c "+$((seen + 1))" "$TMP/out" 2>/dev/null \
            | tr -d '\r' \
            | while IFS= read -r line; do
                  [ -n "$line" ] && stamp "$line"
              done
        seen=$total
    fi
    if ! kill -0 "$NCPID" 2>/dev/null; then
        stamp "*** SERVER CLOSED THE CONNECTION ***"
        exit 0
    fi
    sleep 0.5
    i=$((i + 1))
done

stamp "still connected after ${HOLD}s — server never dropped this client"
