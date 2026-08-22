#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# scripts/sim/lib.sh — shared helpers for the ft_irc simulation harness.
#
# Sourced by simulation.sh, shutdown_simulation.sh and verify_names.sh. Never
# run directly.
#
# Design notes that cost real debugging time (do not "simplify" these away):
#
#  * Every nc client's stdin is a FIFO with a PERMANENT holder process
#    (`sleep infinity > fifo`). A FIFO delivers EOF when the LAST writer
#    closes, so without the holder the first `sim_send` — which opens, writes
#    and closes — would make nc see EOF and exit. The holder is also the
#    shutdown lever: killing it gives nc a clean EOF.
#
#  * HexChat rewrites its process title to a bare "hexchat", so `pkill -f`
#    on the config directory finds nothing. PIDs are tracked in files, and
#    teardown walks the process tree with pgrep -P.
#
#  * Bash's printf '%(fmt)T' is a BUILTIN — timestamping a client log with it
#    costs no fork per line. $(date) there would fork once per received line
#    and fall over the moment you flood a channel.
# ---------------------------------------------------------------------------

# --- paths -----------------------------------------------------------------

sim_repo_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}

: "${SIM_DIR:=$(sim_repo_root)/.sim}"
: "${IRC_HOST:=127.0.0.1}"
: "${IRC_PORT:=6667}"
: "${IRC_PASSWORD:=simpass}"

SIM_ENV_FILE="$SIM_DIR/sim.env"

# Load the environment of a running simulation, so that a *later* invocation
# (--send, --status, shutdown) talks to the same server and state dir.
sim_load_env() {
    if [ -f "$SIM_ENV_FILE" ]; then
        # shellcheck disable=SC1090
        . "$SIM_ENV_FILE"
        return 0
    fi
    return 1
}

# Every value is quoted: this file is sourced, and SIM_STARTED alone contains
# a space, which unquoted turns the timestamp into a command.
sim_save_env() {
    cat > "$SIM_ENV_FILE" <<EOF
IRC_HOST='$IRC_HOST'
IRC_PORT='$IRC_PORT'
IRC_PASSWORD='$IRC_PASSWORD'
SIM_DIR='$SIM_DIR'
SIM_STARTED='$(date '+%Y-%m-%d %H:%M:%S')'
SIM_OWN_SERVER='$SIM_OWN_SERVER'
EOF
}

# --- output ----------------------------------------------------------------

if [ -t 1 ]; then
    C_RESET=$'\033[0m'; C_DIM=$'\033[2m'; C_B=$'\033[1m'
    C_OK=$'\033[32m'; C_ERR=$'\033[31m'; C_WARN=$'\033[33m'; C_INFO=$'\033[36m'
else
    C_RESET=""; C_DIM=""; C_B=""; C_OK=""; C_ERR=""; C_WARN=""; C_INFO=""
fi

say()  { printf '%s==>%s %s\n' "$C_INFO" "$C_RESET" "$*"; }
ok()   { printf '  %s[ok]%s   %s\n' "$C_OK" "$C_RESET" "$*"; }
warn() { printf '  %s[warn]%s %s\n' "$C_WARN" "$C_RESET" "$*"; }
err()  { printf '  %s[err]%s  %s\n' "$C_ERR" "$C_RESET" "$*" >&2; }
dim()  { printf '%s%s%s\n' "$C_DIM" "$*" "$C_RESET"; }

# --- process bookkeeping ---------------------------------------------------

# sim_record_pid <name> <pid>
sim_record_pid() {
    mkdir -p "$SIM_DIR/pids"
    printf '%s\n' "$2" > "$SIM_DIR/pids/$1.pid"
}

sim_read_pid() {
    [ -f "$SIM_DIR/pids/$1.pid" ] && cat "$SIM_DIR/pids/$1.pid"
}

# sim_alive <pid> — true only for a live, non-zombie process. A zombie cannot
# be killed and must not be reported as a shutdown failure.
sim_alive() {
    [ -n "${1:-}" ] || return 1
    kill -0 "$1" 2>/dev/null || return 1
    case "$(ps -o stat= -p "$1" 2>/dev/null | tr -d ' ')" in
        Z*) return 1 ;;
        "") return 1 ;;
    esac
    return 0
}

# sim_kill_stubborn <pid> — parent FIRST, then its (now orphaned) children.
#
# For a process that loops — the scenario and chatter drivers both do — the
# children-first order in sim_kill_tree is a race it can win: kill its
# `sleep`, and the loop simply wakes up and spawns another one before the
# walk gets round to the parent. SIGKILL to the parent first is decisive,
# because a dead shell cannot respawn anything; the children are snapshotted
# beforehand so they can still be reaped afterwards.
sim_kill_stubborn() {
    _ks_pid="${1:-}"
    [ -n "$_ks_pid" ] || return 0
    _ks_kids="$(pgrep -P "$_ks_pid" 2>/dev/null)"
    kill -KILL "$_ks_pid" 2>/dev/null
    for _ks_k in $_ks_kids; do
        sim_kill_tree "$_ks_k" KILL
    done
    return 0
}

# sim_kill_tree <pid> [signal] — children first, then the parent. HexChat's
# proctitle rewrite is why this walks pgrep -P instead of matching cmdlines.
sim_kill_tree() {
    _sk_pid="${1:-}"
    _sk_sig="${2:-TERM}"
    [ -n "$_sk_pid" ] || return 0
    for _sk_child in $(pgrep -P "$_sk_pid" 2>/dev/null); do
        sim_kill_tree "$_sk_child" "$_sk_sig"
    done
    kill "-$_sk_sig" "$_sk_pid" 2>/dev/null
    return 0
}

# --- roster ----------------------------------------------------------------
#
# Roster line:  nick | client | channels | role | realname
#   client   nc | hexchat | auto
#   channels comma-separated, or "-" for none
#   role     op | user
#
# Emits "nick<TAB>client<TAB>channels<TAB>role<TAB>realname" per persona.
sim_parse_roster() {
    _pr_file="$1"
    [ -f "$_pr_file" ] || { err "roster not found: $_pr_file"; return 1; }
    # Only whole-line comments are stripped. A naive 's/#.*//' would eat the
    # channel column, because IRC channel names start with '#' as well.
    sed 's/^[[:space:]]*#.*$//' "$_pr_file" | while IFS='|' read -r _pr_nick _pr_kind _pr_chans _pr_role _pr_real; do
        _pr_nick=$(printf '%s' "${_pr_nick:-}" | tr -d '[:space:]')
        [ -n "$_pr_nick" ] || continue
        _pr_kind=$(printf '%s'  "${_pr_kind:-auto}"  | tr -d '[:space:]')
        _pr_chans=$(printf '%s' "${_pr_chans:--}"    | tr -d '[:space:]')
        _pr_role=$(printf '%s'  "${_pr_role:-user}"  | tr -d '[:space:]')
        _pr_real=$(printf '%s'  "${_pr_real:-$_pr_nick}" | sed 's/^ *//; s/ *$//')
        [ -n "$_pr_real" ] || _pr_real="$_pr_nick"
        printf '%s\t%s\t%s\t%s\t%s\n' \
            "$_pr_nick" "${_pr_kind:-auto}" "${_pr_chans:--}" \
            "${_pr_role:-user}" "$_pr_real"
    done
}

# The server advertises NICKLEN=9 and TRUNCATES past it — a persona called
# "probeclient" is reachable only as "probeclie". Catching that here is the
# difference between a working roster and an afternoon spent wondering why
# PRIVMSG answers 401.
sim_check_nick() {
    _cn="$1"
    [ ${#_cn} -le 9 ] || return 2                       # would be truncated
    printf '%s' "$_cn" | grep -Eq '^[A-Za-z][]A-Za-z0-9[{}\\|^_-]*$' || return 3
    return 0
}

# --- client lifecycle ------------------------------------------------------

sim_client_dir() { printf '%s/clients/%s' "$SIM_DIR" "$1"; }

sim_client_kind() {
    _ck_meta="$(sim_client_dir "$1")/meta"
    [ -f "$_ck_meta" ] && sed -n 's/^kind=//p' "$_ck_meta"
}

sim_client_list() {
    [ -d "$SIM_DIR/clients" ] || return 0
    for _cl_d in "$SIM_DIR"/clients/*/; do
        [ -d "$_cl_d" ] || continue
        basename "$_cl_d"
    done
}

# sim_start_nc <nick> <realname>
sim_start_nc() {
    _sn_nick="$1"; _sn_real="$2"
    _sn_dir="$(sim_client_dir "$_sn_nick")"
    mkdir -p "$_sn_dir"
    rm -f "$_sn_dir/in.fifo"
    mkfifo "$_sn_dir/in.fifo" || return 1
    : > "$_sn_dir/rx.log"; : > "$_sn_dir/raw.log"; : > "$_sn_dir/tx.log"

    nohup bash "$SIM_LIB_DIR/nc_client.sh" \
        "$IRC_HOST" "$IRC_PORT" \
        "$_sn_dir/in.fifo" "$_sn_dir/raw.log" "$_sn_dir/rx.log" \
        >"$_sn_dir/nc.err" 2>&1 &
    _sn_ncpid=$!
    disown 2>/dev/null || true

    # Permanent writer so transient sim_send writers never signal EOF to nc.
    nohup sleep infinity > "$_sn_dir/in.fifo" 2>/dev/null &
    _sn_hold=$!
    disown 2>/dev/null || true

    printf 'kind=nc\nrealname=%s\n' "$_sn_real" > "$_sn_dir/meta"
    sim_record_pid "client-$_sn_nick"      "$_sn_ncpid"
    sim_record_pid "hold-$_sn_nick"        "$_sn_hold"
    return 0
}

# sim_start_hexchat <nick> <realname> <channels-csv>
sim_start_hexchat() {
    _sh_nick="$1"; _sh_real="$2"; _sh_chans="$3"
    _sh_dir="$(sim_client_dir "$_sh_nick")"
    _sh_cfg="$SIM_DIR/hexchat/$_sh_nick"
    mkdir -p "$_sh_dir"
    bash "$SIM_LIB_DIR/hexchat_profile.sh" \
        "$_sh_cfg" "$_sh_nick" "$_sh_real" "$_sh_chans" \
        "$IRC_HOST" "$IRC_PORT" "$IRC_PASSWORD" || return 1

    nohup hexchat -d "$_sh_cfg" >"$_sh_dir/hexchat.err" 2>&1 &
    _sh_pid=$!
    disown 2>/dev/null || true

    printf 'kind=hexchat\nrealname=%s\ncfgdir=%s\n' "$_sh_real" "$_sh_cfg" \
        > "$_sh_dir/meta"
    sim_record_pid "client-$_sh_nick" "$_sh_pid"
    return 0
}

# sim_client_channels <nick> — which channels this client is actually in.
#
# The two client kinds leave different evidence, so this cannot be one grep:
# an nc client has raw.log (its own JOIN echoes), while a HexChat client keeps
# one log file per tab under its config dir and has no raw.log at all.
sim_client_channels() {
    _cc_nick="$1"
    case "$(sim_client_kind "$_cc_nick")" in
        hexchat)
            _cc_logs="$SIM_DIR/hexchat/$_cc_nick/logs/ftircsim"
            [ -d "$_cc_logs" ] || return 0
            for _cc_f in "$_cc_logs"/\#*.log; do
                [ -f "$_cc_f" ] || continue
                basename "$_cc_f" .log
            done | sort -u | tr '\n' ',' | sed 's/,$//'
            ;;
        *)
            _cc_raw="$(sim_client_dir "$_cc_nick")/raw.log"
            [ -f "$_cc_raw" ] || return 0
            grep -ao " JOIN [^ ]*" "$_cc_raw" 2>/dev/null \
                | awk "{print \$2}" | tr -d ":\r" | sort -u \
                | tr "\n" "," | sed "s/,$//"
            ;;
    esac
}

# --- sending ---------------------------------------------------------------
#
# One interface for both client kinds: a RAW IRC line. HexChat clients get it
# through /quote, which hands the line to the server untouched — so the same
# `sim_send alice 'JOIN #x'` works whether alice is an nc socket or a GUI.
sim_send() {
    _ss_nick="$1"; shift
    _ss_line="$*"
    _ss_dir="$(sim_client_dir "$_ss_nick")"
    [ -d "$_ss_dir" ] || { err "no such client: $_ss_nick"; return 1; }

    printf '%(%H:%M:%S)T >> %s\n' -1 "$_ss_line" >> "$_ss_dir/tx.log"

    case "$(sim_client_kind "$_ss_nick")" in
        hexchat)
            _ss_fifo="$SIM_DIR/hexchat/$_ss_nick/ctl.fifo"
            [ -p "$_ss_fifo" ] || { err "$_ss_nick: no control fifo"; return 1; }
            # 2s guard: if the addon never loaded there is no reader and the
            # open() would block this shell forever.
            timeout 2 sh -c "printf 'quote %s\\n' \"\$1\" > \"\$0\"" \
                "$_ss_fifo" "$_ss_line" \
                || { err "$_ss_nick: control fifo has no reader (addon not loaded?)"; return 1; }
            ;;
        *)
            _ss_fifo="$_ss_dir/in.fifo"
            [ -p "$_ss_fifo" ] || { err "$_ss_nick: no input fifo"; return 1; }
            printf '%s\r\n' "$_ss_line" >> "$_ss_fifo"
            ;;
    esac
    return 0
}

# sim_cmd <nick> <hexchat command>  — GUI-only commands (/clear, /part, ...).
# On an nc client the closest raw equivalent is just sent as-is.
sim_cmd() {
    _sc_nick="$1"; shift
    if [ "$(sim_client_kind "$_sc_nick")" = "hexchat" ]; then
        _sc_fifo="$SIM_DIR/hexchat/$_sc_nick/ctl.fifo"
        timeout 2 sh -c "printf '%s\\n' \"\$1\" > \"\$0\"" "$_sc_fifo" "$*"
    else
        sim_send "$_sc_nick" "$@"
    fi
}

sim_say() {
    _sy_nick="$1"; _sy_target="$2"; shift 2
    sim_send "$_sy_nick" "PRIVMSG $_sy_target :$*"
}

# --- waiting ---------------------------------------------------------------

# sim_wait_for <nick> <pattern> [timeout-seconds]
sim_wait_for() {
    _wf_nick="$1"; _wf_pat="$2"; _wf_max="${3:-5}"
    _wf_log="$(sim_client_dir "$_wf_nick")/raw.log"
    _wf_n=0
    _wf_ticks=$(( _wf_max * 10 ))
    while [ "$_wf_n" -lt "$_wf_ticks" ]; do
        if [ -f "$_wf_log" ] && grep -aq "$_wf_pat" "$_wf_log"; then
            return 0
        fi
        sleep 0.1
        _wf_n=$(( _wf_n + 1 ))
    done
    return 1
}

# sim_wait_joined <nick> <channels-csv> [timeout-seconds]
#
# Block until the server itself agrees that <nick> is registered and present
# in every one of <channels>. Asked through a throwaway probe connection, so
# it works for a HexChat GUI exactly as it does for an nc client — neither
# leaves anything this script could poll locally.
#
# This exists because a HexChat client takes seconds to start, connect and
# autojoin, while an nc client is in within milliseconds. Launching a GUI
# persona and moving straight on lets the nc personas behind it create the
# channels first — which silently breaks the "ops connect first, so the first
# op listing a channel owns it" guarantee the whole roster depends on. The
# symptom is an operator persona that quietly is not one, and every MODE it
# is later asked to run answering 482.
sim_wait_joined() {
    _wj_nick="$1"
    _wj_chans="$2"
    _wj_max="${3:-25}"
    _wj_n=0
    while [ "$_wj_n" -lt "$_wj_max" ]; do
        _wj_out="$( { printf 'PASS %s\r\nNICK wjp%s\r\nUSER w 0 * :W\r\nWHOIS %s\r\nQUIT\r\n' \
                        "$IRC_PASSWORD" "$$" "$_wj_nick"; sleep 0.4; } \
                    | timeout 3 nc "$IRC_HOST" "$IRC_PORT" 2>/dev/null )"
        if printf '%s' "$_wj_out" | grep -aq " 311 "; then
            if [ "$_wj_chans" = "-" ] || [ -z "$_wj_chans" ]; then
                return 0
            fi
            # 319 lists the channels, each optionally prefixed with @.
            _wj_line="$(printf '%s' "$_wj_out" | grep -a " 319 " | tr -d '\r')"
            _wj_missing=0
            _wj_rest="$_wj_chans"
            while [ -n "$_wj_rest" ]; do
                _wj_one="${_wj_rest%%,*}"
                [ "$_wj_one" = "$_wj_rest" ] && _wj_rest="" || _wj_rest="${_wj_rest#*,}"
                [ -n "$_wj_one" ] || continue
                case " $_wj_line " in
                    *"$_wj_one"*) : ;;
                    *) _wj_missing=1 ;;
                esac
            done
            [ "$_wj_missing" -eq 0 ] && return 0
        fi
        sleep 1
        _wj_n=$(( _wj_n + 1 ))
    done
    return 1
}

sim_server_up() {
    (exec 3<>"/dev/tcp/$IRC_HOST/$IRC_PORT") 2>/dev/null
}
