#!/usr/bin/env bash
# Optional, slow, and not run by default from run_all.sh (pass --with-valgrind).
# Starts ircserv under valgrind, throws the same functional tests at it, then
# checks the leak/error summary. This will NOT catch every bug the functional
# tests do (valgrind slows the event loop down a lot, so timing-sensitive
# checks like the slow-client test are skipped here) — it's specifically for
# memory correctness, run it as a separate pass before submission.
set -uo pipefail
cd "$(dirname "$0")/.." && source config.sh

command -v valgrind >/dev/null 2>&1 || { echo "  [SKIP] valgrind not installed"; exit 0; }
[[ -x "$BIN" ]] || { echo "  [SKIP] $BIN not found — build first"; exit 0; }

echo "=== 11: memory checks (valgrind) ==="

LOG=/tmp/ftirc_valgrind.log
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=42 -- "$BIN" "$IRC_PORT" "$IRC_PASSWORD" > "$LOG" 2>&1 &
vg_pid=$!
trap 'kill "$vg_pid" 2>/dev/null; wait "$vg_pid" 2>/dev/null' EXIT

for _ in $(seq 1 50); do
    (exec 3<>"/dev/tcp/$IRC_HOST/$IRC_PORT") 2>/dev/null && { exec 3>&- 3<&-; break; }
    sleep 0.2
done

# Drive some real activity through it — reuse the registration/channel/mode
# scripts since they exercise allocation-heavy paths (client objects, channel
# objects, string parsing).
for t in 02_registration.py 05_privmsg.py 06_channel_join_part.py 07_kick_invite_topic.py 08_modes.py 09_malformed_preauth.py; do
    python3 "tests/$t" > /tmp/ftirc_vg_run_"$t".log 2>&1
done

# clean shutdown so valgrind can print its summary
kill -INT "$vg_pid" 2>/dev/null
wait "$vg_pid" 2>/dev/null
trap - EXIT

echo "  --- valgrind summary ---"
grep -E "definitely lost|indirectly lost|possibly lost|ERROR SUMMARY" "$LOG" | sed 's/^/  /'

lost=$(grep -oE "definitely lost: [0-9,]+ bytes" "$LOG" | grep -oE "[0-9,]+" | head -1 | tr -d ',')
errors=$(grep -oE "ERROR SUMMARY: [0-9]+ errors" "$LOG" | grep -oE "[0-9]+" | head -1)

fail=0
if [[ -n "$lost" && "$lost" != "0" ]]; then
    echo "  [FAIL] definitely-lost bytes: $lost"
    fail=1
else
    echo "  [PASS] no definitely-lost bytes"
fi
if [[ -n "$errors" && "$errors" != "0" ]]; then
    echo "  [FAIL] valgrind reported $errors error(s) — see $LOG"
    fail=1
else
    echo "  [PASS] valgrind reports 0 errors"
fi

echo "  full log: $LOG"
[[ $fail -eq 0 ]]
