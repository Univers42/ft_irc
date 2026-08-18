#!/usr/bin/env bash
# Optional, slow, not run by default (pass --with-valgrind to run_all.sh).
# Starts ircserv under valgrind, drives the allocation-heavy functional
# scripts at it, then checks the leak/error summary.
#
# Valgrind slows the event loop down a lot, so timing-sensitive scripts (the
# stress/slow-client one) are deliberately left out — this pass is about
# memory correctness only.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "11: memory checks (valgrind)"

if ! command -v valgrind >/dev/null 2>&1; then
    printf '  [SKIP] valgrind not installed\n'
    exit 0
fi
if [ ! -x "$BIN" ]; then
    printf '  [SKIP] %s not found — build first\n' "$BIN"
    exit 0
fi

LOG=/tmp/ftirc_valgrind.log
: > "$LOG"

valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    -- "$BIN" "$IRC_PORT" "$IRC_PASSWORD" > "$LOG" 2>&1 &
vg_pid=$!
cleanup_vg() {
    [ -n "${vg_pid:-}" ] && kill "$vg_pid" 2>/dev/null
    return 0
}
trap cleanup_vg EXIT

# valgrind makes startup slow — wait generously.
i=0
up=0
while [ "$i" -lt 60 ]; do
    if printf '' | timeout 2 nc -z "$IRC_HOST" "$IRC_PORT" >/dev/null 2>&1; then
        up=1
        break
    fi
    sleep 0.5
    i=$((i + 1))
done
if [ "$up" -ne 1 ]; then
    t_fail "server never came up under valgrind"
    report_summary
    exit 1
fi
t_ok "server started under valgrind"

# Drive real traffic through it. Shell scripts only — no Python anywhere.
: "${SHELL_UNDER_TEST:=bash}"
for t in 02_registration.sh 05_privmsg.sh 06_channel_join_part.sh \
         07_kick_invite_topic.sh 08_modes.sh 09_malformed_preauth.sh; do
    if [ -f "./$t" ]; then
        "$SHELL_UNDER_TEST" "./$t" > "/tmp/ftirc_vg_$t.log" 2>&1
        t_ok "drove $t through the valgrind'd server"
    fi
done

# Clean shutdown so valgrind prints its summary.
kill -INT "$vg_pid" 2>/dev/null
i=0
while [ "$i" -lt 30 ]; do
    kill -0 "$vg_pid" 2>/dev/null || break
    sleep 0.5
    i=$((i + 1))
done
kill -9 "$vg_pid" 2>/dev/null
vg_pid=""
trap - EXIT
sleep 1

printf '  --- valgrind summary ---\n'
grep -E "definitely lost|indirectly lost|possibly lost|ERROR SUMMARY" "$LOG" | sed 's/^/  /'

lost=$(grep -oE "definitely lost: [0-9,]+ bytes" "$LOG" | grep -oE "[0-9,]+" | head -1 | tr -d ',')
errors=$(grep -oE "ERROR SUMMARY: [0-9]+ errors" "$LOG" | grep -oE "[0-9]+" | head -1)

if [ -n "$lost" ] && [ "$lost" != "0" ]; then
    t_fail "definitely-lost bytes: $lost"
else
    t_ok "no definitely-lost bytes"
fi
if [ -n "$errors" ] && [ "$errors" != "0" ]; then
    t_fail "valgrind reported $errors error(s) — see $LOG"
else
    t_ok "valgrind reports 0 errors"
fi

printf '  full log: %s\n' "$LOG"
report_summary
