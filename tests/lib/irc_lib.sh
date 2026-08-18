#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# irc_lib.sh — shared shell library for the ft_irc test suite.
#
# Replaces the old Python harness. Every client is a backgrounded `nc` whose
# stdin comes from a FIFO we hold open on a dedicated file descriptor, and
# whose stdout is appended to a per-client log file. Holding the FIFO open on
# an fd is what lets us send a command in pieces without nc seeing EOF — which
# is the whole point when you're hunting partial-recv() bugs in ircserv.
#
# Every variable a function assigns is prefixed `_irc_`. POSIX sh has no
# variable scoping and `local` is not portable, so an unprefixed `i` inside
# irc_expect() would silently destroy the caller's loop counter — which it
# used to do, breaking 10_stress_multiclient.sh.
#
# Dependencies: nc, mkfifo, grep, printf, sleep, wc, tail, cut, tr, kill.
# No Python, no bashisms beyond what tests/00_shell_probe.sh explicitly checks.
# ---------------------------------------------------------------------------

IRC_TMPDIR="${IRC_TMPDIR:-/tmp/ftirc_bash_$$}"
IRC_NEXT_FD=10
IRC_CLIENTS=""

# --- reporting -------------------------------------------------------------

T_TITLE=""
T_PASS=0
T_FAIL=0

report_init() {
    T_TITLE="$1"
    T_PASS=0
    T_FAIL=0
    printf '\n=== %s ===\n' "$T_TITLE"
}

t_ok() {
    T_PASS=$((T_PASS + 1))
    printf '  [PASS] %s\n' "$1"
}

t_fail() {
    T_FAIL=$((T_FAIL + 1))
    printf '  [FAIL] %s\n' "$1"
}

# t_assert <exit-status> "<description>"
t_assert() {
    if [ "$1" -eq 0 ]; then
        t_ok "$2"
    else
        t_fail "$2"
    fi
}

report_summary() {
    _irc_total=$((T_PASS + T_FAIL))
    if [ "$T_FAIL" -eq 0 ]; then
        printf -- '--- %s: %d/%d passed [OK] ---\n' "$T_TITLE" "$T_PASS" "$_irc_total"
        return 0
    fi
    printf -- '--- %s: %d/%d passed [FAILURES] ---\n' "$T_TITLE" "$T_PASS" "$_irc_total"
    return 1
}

# --- setup / teardown ------------------------------------------------------

irc_setup() {
    mkdir -p "$IRC_TMPDIR" || return 1
    IRC_CLIENTS=""
    IRC_NEXT_FD=10
}

irc_teardown() {
    for _irc_name in $IRC_CLIENTS; do
        irc_close "$_irc_name"
    done
    IRC_CLIENTS=""
    rm -rf "$IRC_TMPDIR"
}

# --- connection ------------------------------------------------------------

# irc_connect <name>
# Spawns an nc bound to a FIFO. Registers the client's fd/pid/logfile in
# dynamically-named variables (IRC_FD_<name>, IRC_PID_<name>, ...).
irc_connect() {
    _irc_name="$1"
    _irc_fifo="$IRC_TMPDIR/$_irc_name.in"
    _irc_out="$IRC_TMPDIR/$_irc_name.out"

    rm -f "$_irc_fifo" "$_irc_out"
    mkfifo "$_irc_fifo" || return 1
    : > "$_irc_out"

    nc "$IRC_HOST" "$IRC_PORT" < "$_irc_fifo" > "$_irc_out" 2>/dev/null &
    _irc_pid=$!

    _irc_fd=$IRC_NEXT_FD
    IRC_NEXT_FD=$((IRC_NEXT_FD + 1))

    # Open the write end and keep it open for the life of the client.
    eval "exec $_irc_fd> \"\$_irc_fifo\"" || return 1

    eval "IRC_FD_$_irc_name=$_irc_fd"
    eval "IRC_PID_$_irc_name=$_irc_pid"
    eval "IRC_OUT_$_irc_name=\"\$_irc_out\""
    eval "IRC_MARK_$_irc_name=0"
    IRC_CLIENTS="$IRC_CLIENTS $_irc_name"

    sleep 0.15   # let the TCP connection actually establish
    return 0
}

# irc_close <name>
irc_close() {
    _irc_name="$1"
    eval "_irc_fd=\${IRC_FD_$_irc_name:-}"
    eval "_irc_pid=\${IRC_PID_$_irc_name:-}"
    [ -n "${_irc_fd:-}" ] && eval "exec $_irc_fd>&-" 2>/dev/null
    [ -n "${_irc_pid:-}" ] && kill "$_irc_pid" 2>/dev/null
    eval "IRC_FD_$_irc_name=''"
    eval "IRC_PID_$_irc_name=''"
    return 0
}

# irc_kill_hard <name> — abrupt disconnect, no QUIT, socket just dies.
irc_kill_hard() {
    _irc_name="$1"
    eval "_irc_pid=\${IRC_PID_$_irc_name:-}"
    eval "_irc_fd=\${IRC_FD_$_irc_name:-}"
    [ -n "${_irc_pid:-}" ] && kill -9 "$_irc_pid" 2>/dev/null
    [ -n "${_irc_fd:-}" ] && eval "exec $_irc_fd>&-" 2>/dev/null
    eval "IRC_FD_$_irc_name=''"
    eval "IRC_PID_$_irc_name=''"
    sleep 0.3
    return 0
}

# irc_freeze <name> / irc_thaw <name>
# SIGSTOP the nc so it stops draining its socket — the shell equivalent of a
# client that never read()s. Its receive buffer fills and stays full.
irc_freeze() {
    eval "_irc_pid=\${IRC_PID_$1:-}"
    [ -n "${_irc_pid:-}" ] && kill -STOP "$_irc_pid" 2>/dev/null
    return 0
}

irc_thaw() {
    eval "_irc_pid=\${IRC_PID_$1:-}"
    [ -n "${_irc_pid:-}" ] && kill -CONT "$_irc_pid" 2>/dev/null
    return 0
}

# --- sending ---------------------------------------------------------------

# irc_send <name> <command words...>   — one well-formed CRLF-terminated line
irc_send() {
    _irc_name="$1"
    shift
    _irc_msg="$*"
    eval "_irc_fd=\$IRC_FD_$_irc_name"
    printf '%s\r\n' "$_irc_msg" >&"$_irc_fd"
}

# irc_send_raw <name> <exact string>   — no CRLF added, printf escapes honoured
irc_send_raw() {
    _irc_name="$1"
    shift
    eval "_irc_fd=\$IRC_FD_$_irc_name"
    printf '%b' "$*" >&"$_irc_fd"
}

# irc_send_fragmented <name> <line> [delay]
# Sends the line one character at a time, then the CRLF, so the server is
# guaranteed to see it across several recv() calls.
irc_send_fragmented() {
    _irc_name="$1"
    _irc_line="$2"
    _irc_delay="${3:-0.02}"
    eval "_irc_fd=\$IRC_FD_$_irc_name"
    _irc_len=${#_irc_line}
    _irc_i=1
    # `cut -c N` rather than ${_irc_line:_irc_i:1} on purpose: substring expansion is a
    # bashism, and this suite is meant to run under shells that may not have it.
    while [ "$_irc_i" -le "$_irc_len" ]; do
        _irc_ch=$(printf '%s' "$_irc_line" | cut -c "$_irc_i")
        printf '%s' "$_irc_ch" >&"$_irc_fd"
        sleep "$_irc_delay"
        _irc_i=$((_irc_i + 1))
    done
    printf '\r\n' >&"$_irc_fd"
}

# --- receiving -------------------------------------------------------------

# irc_clear <name> — mark the current end of the log; later greps ignore
# everything before this point. (We can't truncate a file nc holds open.)
irc_clear() {
    _irc_name="$1"
    eval "_irc_out=\$IRC_OUT_$_irc_name"
    _irc_sz=$(wc -c < "$_irc_out" 2>/dev/null || echo 0)
    _irc_sz=$(printf '%s' "$_irc_sz" | tr -d ' ')
    eval "IRC_MARK_$_irc_name=$_irc_sz"
}

# irc_buf <name> — everything received since the last irc_clear
irc_buf() {
    _irc_name="$1"
    eval "_irc_out=\$IRC_OUT_$_irc_name"
    eval "_irc_mark=\${IRC_MARK_$_irc_name:-0}"
    tail -c "+$((_irc_mark + 1))" "$_irc_out" 2>/dev/null
}

# irc_tenths <seconds> — "2", "2.0", "0.5" -> tenths of a second, as an
# integer. Pure shell on purpose: the obvious `awk "BEGIN{...}"` spelling is
# both an extra fork per poll and a brace-list, which some shells mis-expand
# (see hellish issue: command substitution brace-expanded and run twice).
irc_tenths() {
    case "$1" in
        *.*)
            _irc_whole="${1%%.*}"
            _irc_frac="$(printf '%s' "${1#*.}" | cut -c1)"
            ;;
        *)
            _irc_whole="$1"
            _irc_frac=0
            ;;
    esac
    [ -z "$_irc_whole" ] && _irc_whole=0
    [ -z "$_irc_frac" ] && _irc_frac=0
    echo $((_irc_whole * 10 + _irc_frac))
}

# irc_expect <name> <extended-regex> [timeout-seconds]
# Polls until the pattern shows up in the post-mark buffer. Returns 0/1.
irc_expect() {
    _irc_name="$1"
    _irc_pattern="$2"
    _irc_timeout="${3:-2.0}"
    _irc_iters=$(irc_tenths "$_irc_timeout")
    [ "$_irc_iters" -lt 1 ] && _irc_iters=1
    _irc_i=0
    while [ "$_irc_i" -lt "$_irc_iters" ]; do
        if irc_buf "$_irc_name" | grep -qE "$_irc_pattern" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
        _irc_i=$((_irc_i + 1))
    done
    return 1
}

# irc_expect_absent <name> <regex> [wait-seconds] — pattern must NOT appear
irc_expect_absent() {
    sleep "${3:-1.0}"
    if irc_buf "$1" | grep -qE "$2" 2>/dev/null; then
        return 1
    fi
    return 0
}

# --- convenience assertions ------------------------------------------------

# expect_ok <name> <regex> <timeout> "<description>"
expect_ok() {
    if irc_expect "$1" "$2" "$3"; then
        t_ok "$4"
        return 0
    fi
    t_fail "$4"
    return 1
}

# expect_none <name> <regex> <wait> "<description>"
expect_none() {
    if irc_expect_absent "$1" "$2" "$3"; then
        t_ok "$4"
        return 0
    fi
    t_fail "$4"
    return 1
}

# --- protocol helpers ------------------------------------------------------

# IRC_NICKLEN — the server truncates nicks longer than this (it advertises
# NICKLEN in its 005 ISUPPORT line). Registering a longer nick still succeeds,
# but the client ends up under a *different* name, so every later PRIVMSG /
# KICK / INVITE aimed at the name you asked for comes back 401. That silent
# mismatch cost us a "server stalled" false positive in the stress test, so
# irc_register now refuses to let it happen quietly.
IRC_NICKLEN="${IRC_NICKLEN:-9}"

# irc_register <name> <nick> [password]
# Pass the literal string NOPASS as the third arg to skip PASS entirely.
irc_register() {
    _irc_name="$1"
    _irc_nick="$2"
    _irc_pass="${3-$IRC_PASSWORD}"

    # A nick longer than NICKLEN is truncated by the server, so the client
    # would silently be reachable under a name no later command uses. Shout
    # about it rather than producing a mystifying 401 ten assertions later.
    if [ "${#_irc_nick}" -gt "$IRC_NICKLEN" ]; then
        printf '  [WARN] nick "%s" is %d chars, server NICKLEN=%s — it will be\n' \
            "$_irc_nick" "${#_irc_nick}" "$IRC_NICKLEN" >&2
        printf '         truncated and will not be addressable under that name.\n' >&2
    fi

    if [ "$_irc_pass" != "NOPASS" ]; then
        irc_send "$_irc_name" "PASS $_irc_pass"
    fi
    irc_send "$_irc_name" "NICK $_irc_nick"
    irc_send "$_irc_name" "USER $_irc_nick 0 * :Real Name"
    irc_expect "$_irc_name" "(^| )001( |:)|Welcome" 2.0
}

# irc_server_alive — can we still open a fresh TCP connection at all?
irc_server_alive() {
    # The connect attempt runs in a subshell, so fd 3 is opened *and* closed
    # inside it — there is nothing to clean up out here. Do NOT add an
    # `exec 3>&- 3<&- 2>/dev/null` line: `exec` with no command applies its
    # redirections to the current shell permanently, so the `2>/dev/null`
    # silently sends every later diagnostic on this script's stderr to
    # /dev/null. It used to do exactly that, which hid the NICKLEN warnings
    # under bash while hellish (no /dev/tcp) still showed them.
    if (exec 3<>"/dev/tcp/$IRC_HOST/$IRC_PORT") 2>/dev/null; then
        return 0
    fi
    # Fallback for shells without /dev/tcp (hellish does not implement it).
    if command -v nc >/dev/null 2>&1; then
        printf '' | timeout 2 nc -z "$IRC_HOST" "$IRC_PORT" >/dev/null 2>&1 && return 0
    fi
    return 1
}