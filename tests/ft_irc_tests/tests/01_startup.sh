#!/usr/bin/env bash
# Exercises ./ircserv's argument parsing. None of these should ever segfault;
# the bad cases should exit non-zero quickly, the good case should bind and
# actually accept a connection.
set -uo pipefail
cd "$(dirname "$0")/.." && source config.sh

PASS=0
FAIL=0
ok()  { PASS=$((PASS+1)); echo "  [PASS] $1"; }
bad() { FAIL=$((FAIL+1)); echo "  [FAIL] $1"; }

[[ -x "$BIN" ]] || { echo "  [SKIP] $BIN not found/executable — build first"; exit 0; }

echo "=== 01: startup / argv handling ==="

run_bad_case() {
    local desc="$1"; shift
    timeout 1 "$BIN" "$@" >/tmp/ftirc_stdout.log 2>/tmp/ftirc_stderr.log
    local rc=$?
    if [[ $rc -eq 124 ]]; then
        bad "$desc: process didn't exit (had to be killed after 1s) — should reject and exit immediately"
    elif [[ $rc -eq 0 ]]; then
        bad "$desc: exited 0 (success) for an invalid invocation"
    elif [[ $rc -ge 128 ]]; then
        bad "$desc: died from a signal (rc=$rc) — looks like a crash, not a clean error"
    else
        ok "$desc: rejected with a clean non-zero exit ($rc)"
    fi
}

run_bad_case "no arguments"                 
run_bad_case "port only, no password"       "$STARTUP_PORT"
run_bad_case "too many arguments"           "$STARTUP_PORT" "$IRC_PASSWORD" "extra"
run_bad_case "non-numeric port"             "abc" "$IRC_PASSWORD"
run_bad_case "out-of-range port"            "999999" "$IRC_PASSWORD"
run_bad_case "negative port"                "-1" "$IRC_PASSWORD"

# --- valid startup -------------------------------------------------------
"$BIN" "$STARTUP_PORT" "$IRC_PASSWORD" &
srv_pid=$!
trap '[[ -n "${srv_pid:-}" ]] && kill "$srv_pid" 2>/dev/null' EXIT

up=0
for _ in $(seq 1 20); do
    if (exec 3<>"/dev/tcp/$IRC_HOST/$STARTUP_PORT") 2>/dev/null; then
        exec 3>&- 3<&-
        up=1
        break
    fi
    sleep 0.1
done
if [[ $up -eq 1 ]]; then
    ok "valid invocation binds and accepts a TCP connection"
else
    bad "valid invocation never became connectable on port $STARTUP_PORT"
fi
kill -0 "$srv_pid" 2>/dev/null && ok "server process still alive after one client connected" \
                                || bad "server process died after a single connection"

# --- port already in use -------------------------------------------------------
if [[ $up -eq 1 ]]; then
    if timeout 1 "$BIN" "$STARTUP_PORT" "$IRC_PASSWORD" >/tmp/ftirc_stdout2.log 2>&1; then
        bad "second instance on an already-bound port exited 0 instead of erroring"
    else
        ok "second instance on an already-bound port errors out instead of hanging"
    fi
fi

kill "$srv_pid" 2>/dev/null
wait "$srv_pid" 2>/dev/null
trap - EXIT

echo "--- 01: startup: $PASS/$((PASS+FAIL)) passed ---"
[[ $FAIL -eq 0 ]]
