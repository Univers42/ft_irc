#!/usr/bin/env bash
# Usage:
#   ./run_all.sh                 run the full functional suite
#   ./run_all.sh --skip-build    reuse the existing binary
#   ./run_all.sh --with-valgrind also run 11_memory_checks.sh at the end
#
# Config (port/password/paths) lives in config.sh — edit it once for your project.
set -uo pipefail
cd "$(dirname "$0")"
source config.sh

SKIP_BUILD=0
WITH_VALGRIND=0
for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --with-valgrind) WITH_VALGRIND=1 ;;
    esac
done

declare -A RESULTS
ORDER=()

run_file() {
    local file="$1"
    local name; name=$(basename "$file")
    ORDER+=("$name")
    if [[ "$file" == *.py ]]; then
        python3 "$file"
    else
        bash "$file"
    fi
    RESULTS["$name"]=$?
}

echo "############################################"
echo "# ft_irc test suite"
echo "# BIN=$BIN  PORT=$IRC_PORT  PASSWORD=$IRC_PASSWORD"
echo "############################################"

# --- 0: build/norm checks (no server needed) -------------------------------------------------------
if [[ $SKIP_BUILD -eq 0 ]]; then
    run_file "tests/00_build_norm.sh"
else
    echo "(skipping build/norm — --skip-build was passed)"
fi

# --- 1: argv/startup checks (spins its own server up/down on STARTUP_PORT) -------------------------------------------------------
run_file "tests/01_startup.sh"

# --- start the long-lived server the rest of the suite talks to -------------------------------------------------------
if [[ ! -x "$BIN" ]]; then
    echo "FATAL: $BIN not found or not executable. Build the project or fix config.sh." >&2
    exit 1
fi

"$BIN" "$IRC_PORT" "$IRC_PASSWORD" &
SERVER_PID=$!
cleanup() { kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; }
trap cleanup EXIT

up=0
for _ in $(seq 1 30); do
    (exec 3<>"/dev/tcp/$IRC_HOST/$IRC_PORT") 2>/dev/null && { exec 3>&- 3<&-; up=1; break; }
    sleep 0.1
done
if [[ $up -eq 0 ]]; then
    echo "FATAL: server never became reachable on $IRC_HOST:$IRC_PORT" >&2
    exit 1
fi
echo "server up (pid $SERVER_PID)"

# --- functional suite -------------------------------------------------------
for f in tests/02_registration.py tests/03_tcp_framing.py tests/04_disconnect.py \
         tests/05_privmsg.py tests/06_channel_join_part.py tests/07_kick_invite_topic.py \
         tests/08_modes.py tests/09_malformed_preauth.py tests/10_stress_multiclient.py; do
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        run_file "$f"
    else
        echo "!! server process died before $f could run — treating as failure and stopping here"
        RESULTS["$(basename "$f")"]=1
        ORDER+=("$(basename "$f")")
        break
    fi
done

# --- optional memory pass (starts its own server instance) -------------------------------------------------------
if [[ $WITH_VALGRIND -eq 1 ]]; then
    cleanup
    trap - EXIT
    run_file "tests/11_memory_checks.sh"
else
    cleanup
    trap - EXIT
fi

echo
echo "############################################"
echo "# SUMMARY"
echo "############################################"
overall=0
for name in "${ORDER[@]}"; do
    rc=${RESULTS[$name]}
    if [[ $rc -eq 0 ]]; then
        printf "  %-32s OK\n" "$name"
    else
        printf "  %-32s FAILED\n" "$name"
        overall=1
    fi
done
[[ $overall -eq 0 ]] && echo "All suites passed." || echo "One or more suites failed — see output above."
exit $overall
