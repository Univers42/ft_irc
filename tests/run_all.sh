#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_all.sh — run the whole ft_irc shell suite.
#
#   ./run_all.sh                      everything except the valgrind pass
#   ./run_all.sh --with-valgrind      ... plus 11_memory_checks.sh
#   ./run_all.sh --only 05            just the scripts whose number matches
#   ./run_all.sh --skip-build         skip 12_build_norm.sh (it runs make re)
#
# The runner itself is portable POSIX shell, so it can be driven by any shell:
#   bash    ./run_all.sh
#   hellish ./run_all.sh
# Config (ports, password, paths) lives in config.sh.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

# Which shell actually executes each test script. run_dual.sh sets this to
# bash and then to hellish so the two runs can be diffed.
: "${SHELL_UNDER_TEST:=bash}"
export SHELL_UNDER_TEST

WITH_VALGRIND=0
SKIP_BUILD=0
ONLY=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --with-valgrind) WITH_VALGRIND=1 ;;
        --skip-build)    SKIP_BUILD=1 ;;
        --only)          shift; ONLY="$1" ;;
        -h|--help)       sed -n '2,15p' "$0"; exit 0 ;;
        *)               printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

RESULT_LOG="${IRC_RESULTS:-/tmp/ftirc_results.$$}"
: > "$RESULT_LOG"

printf '############################################\n'
printf '# ft_irc shell test suite\n'
printf '# SHELL=%s\n' "$SHELL_UNDER_TEST"
printf '# BIN=%s\n' "$BIN"
printf '# PORT=%s  PASSWORD=%s\n' "$IRC_PORT" "$IRC_PASSWORD"
printf '############################################\n'

# run_file <script> — run it, print its output, record pass/fail.
run_file() {
    file="$1"
    name=$(basename "$file")
    if [ -n "$ONLY" ]; then
        case "$name" in
            *"$ONLY"*) ;;
            *) return 0 ;;
        esac
    fi
    [ -f "$file" ] || return 0
    "$SHELL_UNDER_TEST" "$file"
    rc=$?
    printf '%s %s\n' "$name" "$rc" >> "$RESULT_LOG"
}

# --- checks that need no server ------------------------------------------
run_file ./00_shell_probe.sh
[ "$SKIP_BUILD" -eq 0 ] && run_file ./12_build_norm.sh
run_file ./01_startup.sh

# --- the long-lived server everything else talks to ----------------------
if [ ! -x "$BIN" ]; then
    printf 'FATAL: %s not found or not executable. Build the project first.\n' "$BIN" >&2
    exit 1
fi

# A port already in use is fatal, and has to be checked BEFORE we start ours.
# Our server would lose the bind and exit 1 straight away, but the reachability
# poll below only asks "is something listening on this port" -- the foreign
# server answers yes, so the run would silently test someone else's process
# and then kill a PID that the OS has since handed to somebody else. That is
# how a concurrent second run of this suite makes the first one fail somewhere
# unrelated, minutes later. Refuse to start instead.
if printf '' | timeout 2 nc -z "$IRC_HOST" "$IRC_PORT" >/dev/null 2>&1; then
    printf 'FATAL: %s:%s is already serving — another ircserv (or another run of\n' \
        "$IRC_HOST" "$IRC_PORT" >&2
    printf '       this suite) owns that port. Stop it, or re-run with IRC_PORT=<free port>.\n' >&2
    exit 1
fi

"$BIN" "$IRC_PORT" "$IRC_PASSWORD" >/tmp/ftirc_server.log 2>&1 &
SERVER_PID=$!

# Kill by PID only while that PID is still OUR server. Between the fork and
# here the process may have exited (a lost bind exits 1 immediately), and a
# busy machine recycles PIDs fast enough that a blind `kill $SERVER_PID` can
# land on an unrelated process -- including another session's test server.
server_alive() {
    [ -n "${SERVER_PID:-}" ] || return 1
    kill -0 "$SERVER_PID" 2>/dev/null || return 1
    case "$(ps -o args= -p "$SERVER_PID" 2>/dev/null)" in
        *ircserv*) return 0 ;;
        *) return 1 ;;
    esac
}
stop_server() {
    server_alive && kill "$SERVER_PID" 2>/dev/null
    SERVER_PID=""
    return 0
}
cleanup() {
    stop_server
    return 0
}
trap cleanup EXIT

up=0
i=0
while [ "$i" -lt 40 ]; do
    # Our own process first: if it died (lost bind, bad argv) there is nothing
    # to wait for, and the port poll would only find whoever beat us to it.
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    if printf '' | timeout 2 nc -z "$IRC_HOST" "$IRC_PORT" >/dev/null 2>&1; then
        up=1
        break
    fi
    sleep 0.1
    i=$((i + 1))
done
if [ "$up" -ne 1 ]; then
    printf 'FATAL: server never became reachable on %s:%s\n' "$IRC_HOST" "$IRC_PORT" >&2
    printf '       server log: /tmp/ftirc_server.log\n' >&2
    sed 's/^/       /' /tmp/ftirc_server.log >&2
    exit 1
fi
printf '\nserver up (pid %s)\n' "$SERVER_PID"

# --- functional suite ----------------------------------------------------
for f in ./02_registration.sh ./03_tcp_framing.sh ./04_disconnect.sh \
         ./05_privmsg.sh ./06_channel_join_part.sh ./07_kick_invite_topic.sh \
         ./08_modes.sh ./09_malformed_preauth.sh ./10_stress_multiclient.sh; do
    if server_alive; then
        run_file "$f"
    else
        printf '\n!! server died before %s could run — recording a failure and stopping\n' "$f"
        printf '%s 1\n' "$(basename "$f")" >> "$RESULT_LOG"
        break
    fi
done

stop_server
trap - EXIT
sleep 0.5

# --- grammar conformance + fuzz (starts its own servers) -----------------
# Deliberately outside the shared-server block: it drives malformed and
# hostile input, so it gets servers of its own on their own ports rather
# than leaving the long-lived one in a state the other suites did not expect.
run_file ./13_command_grammar.sh

# --- optional valgrind pass (starts its own server) ----------------------
[ "$WITH_VALGRIND" -eq 1 ] && run_file ./11_memory_checks.sh

# --- summary -------------------------------------------------------------
printf '\n############################################\n'
printf '# SUMMARY\n'
printf '############################################\n'
overall=0
while read -r name rc; do
    [ -z "$name" ] && continue
    if [ "$rc" -eq 0 ]; then
        printf '  %-32s OK\n' "$name"
    else
        printf '  %-32s FAILED (rc=%s)\n' "$name" "$rc"
        overall=1
    fi
done < "$RESULT_LOG"

rm -f "$RESULT_LOG"
if [ "$overall" -eq 0 ]; then
    printf '\nAll suites passed.\n'
else
    printf '\nOne or more suites failed — see the output above.\n'
fi
exit "$overall"
