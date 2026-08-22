#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# simulation.sh — bring up a whole populated ft_irc environment in one command.
#
# Starts ircserv, connects a roster of users (netcat sockets and/or real
# HexChat GUIs), joins them to their channels, hands out operator status, and
# optionally replays a scripted conversation — all in the background, so the
# shell comes straight back to you.
#
#   scripts/simulation.sh                    10 users, all netcat
#   scripts/simulation.sh --hexchat 2        first 2 as real HexChat windows
#   scripts/simulation.sh --verify-names     naming-convention conformance
#   scripts/simulation.sh --status
#   scripts/shutdown_simulation.sh           free everything
#
# Run --help for the full surface.
# ---------------------------------------------------------------------------
set -uo pipefail

SIM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/sim" && pwd)"
export SIM_LIB_DIR
# shellcheck disable=SC1090
. "$SIM_LIB_DIR/lib.sh"

REPO_ROOT="$(sim_repo_root)"

ROSTER="$SIM_LIB_DIR/personas.conf"
SCENARIO="$SIM_LIB_DIR/scenario_default.conf"
HEXCHAT_COUNT=0
MAX_USERS=0
RUN_SCENARIO=1
RUN_CHATTER=0
START_SERVER=1
SIM_OWN_SERVER=0
ACTION="start"
ACTION_ARGS=()

usage() {
    cat <<'EOF'
simulation.sh — populated ft_irc environment, in the background

STARTING
  --port N             server port                     (default 6667)
  --password P         server password                 (default simpass)
  --roster FILE        cast list                       (sim/personas.conf)
  --users N            use only the first N personas
  --hexchat N          launch the first N personas as real HexChat GUIs
  --scenario FILE      conversation to replay          (sim/scenario_default.conf)
  --no-scenario        connect and join, then stay quiet
  --chatter            keep generating small talk until shutdown
  --no-server          attach to an ircserv that is already running

DRIVING A LIVE SIMULATION
  --send NICK LINE     inject a raw IRC line as NICK    (works for both kinds)
  --cmd  NICK COMMAND  inject a HexChat command as NICK (GUI clients)
  --say  NICK TARGET TEXT
  --verify-names       run the naming-convention conformance probe
  --verify-grammar     run the RFC 2812 message-grammar probe
  --fuzz-mode [N]      fuzz the MODE parser (N random cases, default 150)

INSPECTING
  --status             who is up, who is connected, where
  --logs [NICK]        show a client's log (all clients if omitted)
  --tail NICK          follow a client's log
  --grep PATTERN       search every client log at once
  --server-log         show the server console log

  -h, --help           this text

Everything lives under .sim/ in the repo. scripts/shutdown_simulation.sh
frees all of it.
EOF
}

# --- argument parsing ------------------------------------------------------

while [ $# -gt 0 ]; do
    case "$1" in
        --port)        IRC_PORT="$2"; shift 2 ;;
        --password)    IRC_PASSWORD="$2"; shift 2 ;;
        --roster)      ROSTER="$2"; shift 2 ;;
        --users)       MAX_USERS="$2"; shift 2 ;;
        --hexchat)     HEXCHAT_COUNT="$2"; shift 2 ;;
        --scenario)    SCENARIO="$2"; shift 2 ;;
        --no-scenario) RUN_SCENARIO=0; shift ;;
        --chatter)     RUN_CHATTER=1; shift ;;
        --no-server)   START_SERVER=0; shift ;;
        --send)        ACTION="send";    shift; ACTION_ARGS=("$@"); break ;;
        --cmd)         ACTION="cmd";     shift; ACTION_ARGS=("$@"); break ;;
        --say)         ACTION="say";     shift; ACTION_ARGS=("$@"); break ;;
        --verify-names) ACTION="verify"; shift ;;
        --verify-grammar) ACTION="grammar"; shift ;;
        --fuzz-mode)     ACTION="fuzzmode"; shift; ACTION_ARGS=("${1:-}"); [ $# -gt 0 ] && shift ;;
        --status)      ACTION="status";  shift ;;
        --logs)        ACTION="logs";    shift; ACTION_ARGS=("${1:-}"); [ $# -gt 0 ] && shift ;;
        --tail)        ACTION="tail";    shift; ACTION_ARGS=("$1"); shift ;;
        --grep)        ACTION="grep";    shift; ACTION_ARGS=("$1"); shift ;;
        --server-log)  ACTION="serverlog"; shift ;;
        -h|--help)     usage; exit 0 ;;
        *)             err "unknown option: $1"; usage; exit 1 ;;
    esac
done

# --- actions against an already-running simulation -------------------------

require_running() {
    sim_load_env || { err "no simulation running (no $SIM_ENV_FILE)"; exit 1; }
}

case "$ACTION" in
    send)
        require_running
        [ ${#ACTION_ARGS[@]} -ge 2 ] || { err "usage: --send NICK LINE"; exit 1; }
        _n="${ACTION_ARGS[0]}"; unset 'ACTION_ARGS[0]'
        sim_send "$_n" "${ACTION_ARGS[@]}" && ok "sent to $_n"
        exit $?
        ;;
    cmd)
        require_running
        [ ${#ACTION_ARGS[@]} -ge 2 ] || { err "usage: --cmd NICK COMMAND"; exit 1; }
        _n="${ACTION_ARGS[0]}"; unset 'ACTION_ARGS[0]'
        sim_cmd "$_n" "${ACTION_ARGS[@]}" && ok "sent to $_n"
        exit $?
        ;;
    say)
        require_running
        [ ${#ACTION_ARGS[@]} -ge 3 ] || { err "usage: --say NICK TARGET TEXT"; exit 1; }
        _n="${ACTION_ARGS[0]}"; _t="${ACTION_ARGS[1]}"
        unset 'ACTION_ARGS[0]' 'ACTION_ARGS[1]'
        sim_say "$_n" "$_t" "${ACTION_ARGS[@]}" && ok "$_n -> $_t"
        exit $?
        ;;
    verify)
        require_running
        exec bash "$SIM_LIB_DIR/verify_names.sh"
        ;;
    grammar)
        require_running
        exec bash "$SIM_LIB_DIR/verify_grammar.sh"
        ;;
    fuzzmode)
        require_running
        exec bash "$SIM_LIB_DIR/fuzz_mode.sh" ${ACTION_ARGS[0]:-}
        ;;
    status)
        require_running
        say "simulation on $IRC_HOST:$IRC_PORT   (started $SIM_STARTED)"
        _srv="$(sim_read_pid server)"
        if [ -n "$_srv" ] && sim_alive "$_srv"; then
            ok "ircserv running (pid $_srv)"
        elif [ "${SIM_OWN_SERVER:-0}" = "0" ]; then
            ok "using an externally started ircserv"
        else
            err "ircserv is NOT running"
        fi
        printf '\n  %-10s %-8s %-7s %s\n' CLIENT KIND STATE CHANNELS
        printf '  %-10s %-8s %-7s %s\n' ---------- -------- ------- --------
        for _c in $(sim_client_list); do
            _kind="$(sim_client_kind "$_c")"
            _pid="$(sim_read_pid "client-$_c")"
            if sim_alive "$_pid"; then _state="up"; else _state="down"; fi
            _chans="$(sim_client_channels "$_c")"
            printf '  %-10s %-8s %-7s %s\n' "$_c" "$_kind" "$_state" "${_chans:--}"
        done
        printf '\n  logs: %s/clients/<nick>/rx.log\n' "$SIM_DIR"
        exit 0
        ;;
    logs)
        require_running
        _who="${ACTION_ARGS[0]:-}"
        if [ -n "$_who" ]; then
            cat "$(sim_client_dir "$_who")/rx.log"
        else
            for _c in $(sim_client_list); do
                printf '\n%s===== %s =====%s\n' "$C_B" "$_c" "$C_RESET"
                cat "$(sim_client_dir "$_c")/rx.log" 2>/dev/null
            done
        fi
        exit 0
        ;;
    tail)
        require_running
        exec tail -f "$(sim_client_dir "${ACTION_ARGS[0]}")/rx.log"
        ;;
    grep)
        require_running
        for _c in $(sim_client_list); do
            _hits="$(grep -a "${ACTION_ARGS[0]}" "$(sim_client_dir "$_c")/rx.log" 2>/dev/null)"
            [ -n "$_hits" ] && printf '%s%s:%s\n%s\n' "$C_B" "$_c" "$C_RESET" "$_hits"
        done
        exit 0
        ;;
    serverlog)
        require_running
        sed 's/\x1b\[[0-9;]*m//g' "$SIM_DIR/server.log"
        exit 0
        ;;
esac

# --- start a new simulation ------------------------------------------------

if [ -f "$SIM_ENV_FILE" ]; then
    err "a simulation is already up (see --status)."
    err "run scripts/shutdown_simulation.sh first."
    exit 1
fi

say "ft_irc simulation — $IRC_HOST:$IRC_PORT"

# Roster first: a bad nick must not cost you a half-built environment.
mapfile -t PERSONAS < <(sim_parse_roster "$ROSTER") || exit 1
[ "${#PERSONAS[@]}" -gt 0 ] || { err "roster is empty: $ROSTER"; exit 1; }
if [ "$MAX_USERS" -gt 0 ] && [ "${#PERSONAS[@]}" -gt "$MAX_USERS" ]; then
    PERSONAS=("${PERSONAS[@]:0:$MAX_USERS}")
fi

roster_bad=0
for _p in "${PERSONAS[@]}"; do
    _nick="${_p%%$'\t'*}"
    sim_check_nick "$_nick"
    case $? in
        2) err "roster nick '$_nick' is ${#_nick} chars; the server truncates at 9"; roster_bad=1 ;;
        3) err "roster nick '$_nick' is not a legal RFC 2812 nickname"; roster_bad=1 ;;
    esac
done
[ "$roster_bad" -eq 0 ] || { err "fix the roster and try again"; exit 1; }
ok "roster: ${#PERSONAS[@]} personas, all names legal"

mkdir -p "$SIM_DIR/clients" "$SIM_DIR/pids" "$SIM_DIR/hexchat"

# --- server ---
if [ "$START_SERVER" -eq 1 ]; then
    if sim_server_up; then
        err "something is already listening on $IRC_HOST:$IRC_PORT"
        err "use --port, or --no-server to attach to it"
        rm -rf "$SIM_DIR"; exit 1
    fi
    [ -x "$REPO_ROOT/ircserv" ] || { err "no ircserv binary — run make"; rm -rf "$SIM_DIR"; exit 1; }
    nohup "$REPO_ROOT/ircserv" "$IRC_PORT" "$IRC_PASSWORD" > "$SIM_DIR/server.log" 2>&1 &
    sim_record_pid server $!
    disown 2>/dev/null || true
    SIM_OWN_SERVER=1
    for _i in 1 2 3 4 5 6 7 8 9 10; do
        sim_server_up && break
        sleep 0.3
    done
    if sim_server_up; then
        ok "ircserv up (pid $(sim_read_pid server))"
    else
        err "ircserv failed to start — see $SIM_DIR/server.log"
        rm -rf "$SIM_DIR"; exit 1
    fi
else
    sim_server_up || { err "nothing listening on $IRC_HOST:$IRC_PORT"; rm -rf "$SIM_DIR"; exit 1; }
    ok "attached to the ircserv already on $IRC_PORT"
fi

sim_save_env

# --- connect the cast ---
# Ops first, so the first op listing a channel is the one that creates it and
# therefore owns it. Everything downstream (the +o grants, the scenario's
# MODE/KICK/INVITE lines) depends on that ordering.
ordered=()
for _p in "${PERSONAS[@]}"; do
    IFS=$'\t' read -r _n _k _c _r _rn <<< "$_p"
    [ "$_r" = "op" ] && ordered+=("$_p")
done
for _p in "${PERSONAS[@]}"; do
    IFS=$'\t' read -r _n _k _c _r _rn <<< "$_p"
    [ "$_r" = "op" ] || ordered+=("$_p")
done

hexchat_left="$HEXCHAT_COUNT"
if [ "$hexchat_left" -gt 0 ] && ! command -v hexchat >/dev/null 2>&1; then
    warn "hexchat is not installed — every client will be netcat"
    hexchat_left=0
fi

say "connecting ${#ordered[@]} clients"
for _p in "${ordered[@]}"; do
    IFS=$'\t' read -r nick kind chans role realname <<< "$_p"

    if [ "$kind" = "auto" ]; then
        if [ "$hexchat_left" -gt 0 ]; then kind="hexchat"; else kind="nc"; fi
    fi
    [ "$kind" = "hexchat" ] && hexchat_left=$(( hexchat_left - 1 ))

    if [ "$kind" = "hexchat" ]; then
        # HexChat does its own PASS/NICK/USER and autojoins from servlist.conf.
        if sim_start_hexchat "$nick" "$realname" "$chans"; then
            # Block until the SERVER agrees this client has joined. A GUI
            # takes seconds where an nc client takes milliseconds, and moving
            # on without waiting lets later personas create the channels
            # first — which silently costs this persona the operator status
            # the roster ordering promised it.
            if sim_wait_joined "$nick" "$chans" 30; then
                ok "$nick (hexchat GUI) -> ${chans}"
            else
                err "$nick: hexchat did not join ${chans} in time"
            fi
        else
            err "$nick: hexchat launch failed"
        fi
    else
        sim_start_nc "$nick" "$realname" || { err "$nick: nc launch failed"; continue; }
        sim_send "$nick" "PASS $IRC_PASSWORD"
        sim_send "$nick" "NICK $nick"
        sim_send "$nick" "USER $nick 0 * :$realname"
        if sim_wait_for "$nick" " 001 " 5; then
            if [ "$chans" != "-" ]; then
                sim_send "$nick" "JOIN $chans"
                ok "$nick (nc) -> ${chans}"
            else
                ok "$nick (nc) -> no channels (lurker)"
            fi
        else
            err "$nick: never registered"
        fi
    fi
done

# --- hand out operator status ---
# The first op on a channel created it and already has +o. Any other op
# persona on that channel is granted it by that owner.
say "assigning channel operators"
declare -A chan_owner=()
for _p in "${ordered[@]}"; do
    IFS=$'\t' read -r nick kind chans role realname <<< "$_p"
    [ "$chans" = "-" ] && continue
    IFS=',' read -ra clist <<< "$chans"
    for ch in "${clist[@]}"; do
        [ -n "$ch" ] || continue
        if [ -z "${chan_owner[$ch]:-}" ]; then
            chan_owner[$ch]="$nick"
            [ "$role" = "op" ] && ok "$ch owned by $nick"
        elif [ "$role" = "op" ]; then
            sim_send "${chan_owner[$ch]}" "MODE $ch +o $nick"
            ok "$ch: +o $nick (granted by ${chan_owner[$ch]})"
            sleep 0.2
        fi
    done
done

# --- background drivers ---
if [ "$RUN_SCENARIO" -eq 1 ] && [ -f "$SCENARIO" ]; then
    nohup bash "$SIM_LIB_DIR/driver.sh" scenario "$SCENARIO" \
        > "$SIM_DIR/scenario.log" 2>&1 &
    sim_record_pid scenario $!
    disown 2>/dev/null || true
    ok "scenario replaying in the background ($(basename "$SCENARIO"))"
fi

if [ "$RUN_CHATTER" -eq 1 ]; then
    nohup bash "$SIM_LIB_DIR/driver.sh" chatter "$ROSTER" \
        > "$SIM_DIR/chatter.log" 2>&1 &
    sim_record_pid chatter $!
    disown 2>/dev/null || true
    ok "chatter running until shutdown"
fi

cat <<EOF

  ${C_B}simulation up${C_RESET}   $IRC_HOST:$IRC_PORT   password: $IRC_PASSWORD

    scripts/simulation.sh --status
    scripts/simulation.sh --tail alice
    scripts/simulation.sh --send judy 'JOIN #general'
    scripts/simulation.sh --verify-names
    scripts/shutdown_simulation.sh

  join it yourself:  nc -C $IRC_HOST $IRC_PORT
  logs:              $SIM_DIR/clients/<nick>/rx.log

EOF
