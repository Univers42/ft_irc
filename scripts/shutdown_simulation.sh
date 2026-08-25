#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# shutdown_simulation.sh — free everything simulation.sh started.
#
#   scripts/shutdown_simulation.sh            stop, keep the logs
#   scripts/shutdown_simulation.sh --purge    stop and delete .sim/ entirely
#   scripts/shutdown_simulation.sh --force    skip the polite QUIT
#
# Order matters. Clients are asked to QUIT first so the server sees a clean
# disconnect and the transcripts end the way a real session would; only then
# are the processes killed. Killing first would leave every log ending
# mid-sentence and the server reaping sockets by RST.
#
# Two things this has to get right:
#
#  * HexChat rewrites its process title to a bare "hexchat", so there is no
#    cmdline to pkill on. PIDs come from .sim/pids/ and the tree is walked
#    with pgrep -P.
#  * A zombie cannot be killed. Reporting one as a failure would make a clean
#    shutdown look broken, so sim_alive() treats state Z as dead.
# ---------------------------------------------------------------------------
set -uo pipefail

SIM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/sim" && pwd)"
export SIM_LIB_DIR
# shellcheck disable=SC1090
. "$SIM_LIB_DIR/lib.sh"

PURGE=0
FORCE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --purge) PURGE=1; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help)
            printf 'usage: shutdown_simulation.sh [--purge] [--force]\n'
            printf '  --purge  also delete %s\n' "$SIM_DIR"
            printf '  --force  do not send QUIT first\n'
            exit 0 ;;
        *) err "unknown option: $1"; exit 1 ;;
    esac
done

# --- find a simulation, wherever it is -------------------------------------
#
# There is more than one state directory now: the automated simulation lives
# in .sim/ and `make man_sim` runs in .sim-manual/ so the two can be up at
# once. That made the obvious command useless -- a bare
# `./scripts/shutdown_simulation.sh` looked only in .sim/, reported "nothing
# to free" and left the sandbox running, with no hint that it had searched
# the wrong place.
#
# So when SIM_DIR was not named explicitly and the default is empty, look
# around before giving up. If several are up, free them all: "shut down the
# simulation" has one obvious meaning and it is not "shut down whichever one
# I happened to guess".
if [ -z "${SIM_DIR_EXPLICIT:-}" ] && [ ! -f "$SIM_ENV_FILE" ]; then
    _found=""
    for _cand in "$(sim_repo_root)"/.sim*/; do
        [ -f "${_cand}sim.env" ] || continue
        _found="$_found ${_cand%/}"
    done
    set -- $_found
    if [ $# -gt 1 ]; then
        say "found $# running simulations — freeing all of them"
        for _dir in "$@"; do
            SIM_DIR="$_dir" SIM_DIR_EXPLICIT=1 \
                bash "$0" $([ "$PURGE" -eq 1 ] && printf -- --purge) \
                          $([ "$FORCE" -eq 1 ] && printf -- --force)
        done
        exit 0
    fi
    if [ $# -eq 1 ]; then
        SIM_DIR="$1"
        SIM_ENV_FILE="$SIM_DIR/sim.env"
        say "using $SIM_DIR"
    fi
fi

if ! sim_load_env; then
    warn "no simulation state at $SIM_ENV_FILE — nothing to free"
    # Still offer to clear a stray directory.
    [ "$PURGE" -eq 1 ] && [ -d "$SIM_DIR" ] && rm -rf "$SIM_DIR" && ok "removed $SIM_DIR"
    exit 0
fi

say "shutting down the simulation on $IRC_HOST:$IRC_PORT"

# --- 1. stop the drivers, so nothing new is injected mid-teardown ---
# TERM first, then SIGKILL anything still standing. A driver sitting between
# two scheduled lines is inside `sleep`; killing its tree once can race the
# next sleep it spawns, and a survivor keeps injecting traffic into a server
# we are about to stop.
for _d in scenario chatter; do
    _p="$(sim_read_pid "$_d")"
    if sim_alive "$_p"; then
        sim_kill_tree "$_p" TERM
        ok "stopped the $_d driver"
    fi
done
sleep 0.5
for _d in scenario chatter; do
    _p="$(sim_read_pid "$_d")"
    if sim_alive "$_p"; then
        sim_kill_stubborn "$_p"
        warn "$_d driver ignored SIGTERM and was killed"
    fi
done

# --- 2. polite QUIT ---
if [ "$FORCE" -eq 0 ]; then
    _quit=0
    for _c in $(sim_client_list); do
        sim_send "$_c" "QUIT :simulation over" >/dev/null 2>&1 && _quit=$(( _quit + 1 ))
    done
    [ "$_quit" -gt 0 ] && ok "sent QUIT to $_quit client(s)"
    sleep 1
fi

# --- 3. kill the clients ---
_killed=0
for _c in $(sim_client_list); do
    for _role in "client-$_c" "hold-$_c"; do
        _p="$(sim_read_pid "$_role")"
        if sim_alive "$_p"; then
            sim_kill_tree "$_p" TERM
            _killed=$(( _killed + 1 ))
        fi
    done
done
[ "$_killed" -gt 0 ] && ok "terminated $_killed client process group(s)"

sleep 1

# Escalate on anything that ignored SIGTERM.
_stubborn=0
for _c in $(sim_client_list); do
    for _role in "client-$_c" "hold-$_c"; do
        _p="$(sim_read_pid "$_role")"
        if sim_alive "$_p"; then
            sim_kill_tree "$_p" KILL
            _stubborn=$(( _stubborn + 1 ))
        fi
    done
done
[ "$_stubborn" -gt 0 ] && warn "SIGKILLed $_stubborn process(es) that ignored SIGTERM"

# --- 4. the server, last, so it sees every client leave first ---
_srv="$(sim_read_pid server)"
if [ "${SIM_OWN_SERVER:-0}" = "1" ] && [ -n "$_srv" ]; then
    if sim_alive "$_srv"; then
        # SIGTERM is the path ircserv is built to exit cleanly through: it
        # drops out of the epoll loop and frees every client and channel.
        kill -TERM "$_srv" 2>/dev/null
        for _i in 1 2 3 4 5 6 7 8 9 10; do
            sim_alive "$_srv" || break
            sleep 0.3
        done
        if sim_alive "$_srv"; then
            kill -KILL "$_srv" 2>/dev/null
            warn "ircserv ignored SIGTERM and was killed (pid $_srv)"
        else
            ok "ircserv stopped cleanly (pid $_srv)"
        fi
    else
        warn "ircserv was already gone"
    fi
elif [ "${SIM_OWN_SERVER:-0}" = "0" ]; then
    ok "left the externally started ircserv running"
fi

# --- 5. fifos ---
for _c in $(sim_client_list); do
    rm -f "$(sim_client_dir "$_c")/in.fifo"
done
[ -d "$SIM_DIR/hexchat" ] && rm -f "$SIM_DIR"/hexchat/*/ctl.fifo 2>/dev/null

# --- 6. final sweep: escalate once, then report ---
# Anything still standing here has already ignored a SIGTERM, so it gets
# SIGKILL parent-first before being called a survivor.
for _f in "$SIM_DIR"/pids/*.pid; do
    [ -f "$_f" ] || continue
    _p="$(cat "$_f")"
    sim_alive "$_p" && sim_kill_stubborn "$_p"
done
sleep 0.5

_leftover=0
for _f in "$SIM_DIR"/pids/*.pid; do
    [ -f "$_f" ] || continue
    _p="$(cat "$_f")"
    if sim_alive "$_p"; then
        err "still alive: $(basename "$_f" .pid) (pid $_p)"
        _leftover=$(( _leftover + 1 ))
    fi
done

if [ "$PURGE" -eq 1 ]; then
    rm -rf "$SIM_DIR"
    ok "purged $SIM_DIR"
else
    rm -f "$SIM_ENV_FILE"
    ok "logs kept in $SIM_DIR (--purge to delete)"
fi

if [ "$_leftover" -gt 0 ]; then
    err "$_leftover process(es) survived shutdown"
    exit 1
fi
say "everything freed"
