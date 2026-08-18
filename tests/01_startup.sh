#!/usr/bin/env bash
# ./ircserv argument parsing. None of these may segfault: the bad cases must
# exit non-zero promptly, the good case must bind and accept a connection.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "01: startup / argv handling"

if [ ! -x "$BIN" ]; then
    printf '  [SKIP] %s not found or not executable — build first\n' "$BIN"
    exit 0
fi

# run_bad_case <description> [args...] — an invalid invocation must exit
# non-zero, quickly, and not from a signal.
run_bad_case() {
    desc="$1"
    shift
    timeout 2 "$BIN" "$@" >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 124 ]; then
        t_fail "$desc: did not exit (killed after 2s) — must reject and exit immediately"
    elif [ "$rc" -eq 0 ]; then
        t_fail "$desc: exited 0 (success) for an invalid invocation"
    elif [ "$rc" -ge 128 ]; then
        t_fail "$desc: died from a signal (rc=$rc) — a crash, not a clean error"
    else
        t_ok "$desc: rejected with a clean non-zero exit ($rc)"
    fi
}

run_bad_case "no arguments"
run_bad_case "port only, no password"   "$STARTUP_PORT"
run_bad_case "too many arguments"       "$STARTUP_PORT" "$IRC_PASSWORD" "extra"
run_bad_case "non-numeric port"         "abc" "$IRC_PASSWORD"
run_bad_case "out-of-range port"        "999999" "$IRC_PASSWORD"
run_bad_case "negative port"            "-1" "$IRC_PASSWORD"
run_bad_case "port 0"                   "0" "$IRC_PASSWORD"
run_bad_case "float port"               "66.67" "$IRC_PASSWORD"
run_bad_case "port with trailing junk"  "6667x" "$IRC_PASSWORD"
run_bad_case "empty port"               "" "$IRC_PASSWORD"
run_bad_case "port 65536 (one past max)" "65536" "$IRC_PASSWORD"

# An empty password is a policy choice, not obviously invalid — just make sure
# it doesn't hang or crash, whichever way it decides.
timeout 2 "$BIN" "$STARTUP_PORT" "" >/dev/null 2>&1
rc=$?
if [ "$rc" -ge 128 ] && [ "$rc" -ne 124 ]; then
    t_fail "empty password: died from a signal (rc=$rc)"
else
    t_ok "empty password: handled without crashing (rc=$rc)"
fi

# --- valid startup --------------------------------------------------------
"$BIN" "$STARTUP_PORT" "$IRC_PASSWORD" >/dev/null 2>&1 &
srv_pid=$!
cleanup_srv() {
    [ -n "${srv_pid:-}" ] && kill "$srv_pid" 2>/dev/null
    return 0
}
trap cleanup_srv EXIT

up=0
i=0
while [ "$i" -lt 30 ]; do
    if printf '' | timeout 2 nc -z "$IRC_HOST" "$STARTUP_PORT" >/dev/null 2>&1; then
        up=1
        break
    fi
    sleep 0.1
    i=$((i + 1))
done

if [ "$up" -eq 1 ]; then
    t_ok "valid invocation binds and accepts a TCP connection"
else
    t_fail "valid invocation never became connectable on port $STARTUP_PORT"
fi

if kill -0 "$srv_pid" 2>/dev/null; then
    t_ok "server process still alive after a client connected"
else
    t_fail "server process died after a single connection"
fi

# --- port already in use --------------------------------------------------
if [ "$up" -eq 1 ]; then
    timeout 3 "$BIN" "$STARTUP_PORT" "$IRC_PASSWORD" >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        t_fail "second instance on an already-bound port exited 0 instead of erroring"
    elif [ "$rc" -eq 124 ]; then
        t_fail "second instance on an already-bound port hung instead of erroring"
    else
        t_ok "second instance on an already-bound port errors out (rc=$rc)"
    fi
fi

kill "$srv_pid" 2>/dev/null
srv_pid=""
trap - EXIT

report_summary
