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
# Dependencies: nc, mkfifo, grep, printf, sleep, awk, wc, tail, kill.
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
    total=$((T_PASS + T_FAIL))
    if [ "$T_FAIL" -eq 0 ]; then
        printf -- '--- %s: %d/%d passed [OK] ---\n' "$T_TITLE" "$T_PASS" "$total"
        return 0
    fi
    printf -- '--- %s: %d/%d passed [FAILURES] ---\n' "$T_TITLE" "$T_PASS" "$total"
    return 1
}

# --- setup / teardown ------------------------------------------------------

irc_setup() {
    mkdir -p "$IRC_TMPDIR" || return 1
    IRC_CLIENTS=""
    IRC_NEXT_FD=10
}

irc_teardown() {
    for name in $IRC_CLIENTS; do
        irc_close "$name"
    done
    IRC_CLIENTS=""
    rm -rf "$IRC_TMPDIR"
}

# --- connection ------------------------------------------------------------

# irc_connect <name>
# Spawns an nc bound to a FIFO. Registers the client's fd/pid/logfile in
# dynamically-named variables (IRC_FD_<name>, IRC_PID_<name>, ...).
irc_connect() {
    name="$1"
    fifo="$IRC_TMPDIR/$name.in"
    out="$IRC_TMPDIR/$name.out"

    rm -f "$fifo" "$out"
    mkfifo "$fifo" || return 1
    : > "$out"

    nc "$IRC_HOST" "$IRC_PORT" < "$fifo" > "$out" 2>/dev/null &
    pid=$!

    fd=$IRC_NEXT_FD
    IRC_NEXT_FD=$((IRC_NEXT_FD + 1))

    # Open the write end and keep it open for the life of the client.
    eval "exec $fd> \"\$fifo\"" || return 1

    eval "IRC_FD_$name=$fd"
    eval "IRC_PID_$name=$pid"
    eval "IRC_OUT_$name=\"\$out\""
    eval "IRC_MARK_$name=0"
    IRC_CLIENTS="$IRC_CLIENTS $name"

    sleep 0.15   # let the TCP connection actually establish
    return 0
}

# irc_close <name>
irc_close() {
    name="$1"
    eval "fd=\${IRC_FD_$name:-}"
    eval "pid=\${IRC_PID_$name:-}"
    [ -n "${fd:-}" ] && eval "exec $fd>&-" 2>/dev/null
    [ -n "${pid:-}" ] && kill "$pid" 2>/dev/null
    eval "IRC_FD_$name=''"
    eval "IRC_PID_$name=''"
    return 0
}

# irc_kill_hard <name> — abrupt disconnect, no QUIT, socket just dies.
irc_kill_hard() {
    name="$1"
    eval "pid=\${IRC_PID_$name:-}"
    eval "fd=\${IRC_FD_$name:-}"
    [ -n "${pid:-}" ] && kill -9 "$pid" 2>/dev/null
    [ -n "${fd:-}" ] && eval "exec $fd>&-" 2>/dev/null
    eval "IRC_FD_$name=''"
    eval "IRC_PID_$name=''"
    sleep 0.3
    return 0
}

# irc_freeze <name> / irc_thaw <name>
# SIGSTOP the nc so it stops draining its socket — the shell equivalent of a
# client that never read()s. Its receive buffer fills and stays full.
irc_freeze() {
    eval "pid=\${IRC_PID_$1:-}"
    [ -n "${pid:-}" ] && kill -STOP "$pid" 2>/dev/null
    return 0
}

irc_thaw() {
    eval "pid=\${IRC_PID_$1:-}"
    [ -n "${pid:-}" ] && kill -CONT "$pid" 2>/dev/null
    return 0
}

# --- sending ---------------------------------------------------------------

# irc_send <name> <command words...>   — one well-formed CRLF-terminated line
irc_send() {
    name="$1"
    shift
    msg="$*"
    eval "fd=\$IRC_FD_$name"
    printf '%s\r\n' "$msg" >&"$fd"
}

# irc_send_raw <name> <exact string>   — no CRLF added, printf escapes honoured
irc_send_raw() {
    name="$1"
    shift
    eval "fd=\$IRC_FD_$name"
    printf '%b' "$*" >&"$fd"
}

# irc_send_fragmented <name> <line> [delay]
# Sends the line one character at a time, then the CRLF, so the server is
# guaranteed to see it across several recv() calls.
irc_send_fragmented() {
    name="$1"
    line="$2"
    delay="${3:-0.02}"
    eval "fd=\$IRC_FD_$name"
    len=${#line}
    i=1
    # `cut -c N` rather than ${line:i:1} on purpose: substring expansion is a
    # bashism, and this suite is meant to run under shells that may not have it.
    while [ "$i" -le "$len" ]; do
        ch=$(printf '%s' "$line" | cut -c "$i")
        printf '%s' "$ch" >&"$fd"
        sleep "$delay"
        i=$((i + 1))
    done
    printf '\r\n' >&"$fd"
}

# --- receiving -------------------------------------------------------------

# irc_clear <name> — mark the current end of the log; later greps ignore
# everything before this point. (We can't truncate a file nc holds open.)
irc_clear() {
    name="$1"
    eval "out=\$IRC_OUT_$name"
    sz=$(wc -c < "$out" 2>/dev/null || echo 0)
    sz=$(printf '%s' "$sz" | tr -d ' ')
    eval "IRC_MARK_$name=$sz"
}

# irc_buf <name> — everything received since the last irc_clear
irc_buf() {
    name="$1"
    eval "out=\$IRC_OUT_$name"
    eval "mark=\${IRC_MARK_$name:-0}"
    tail -c "+$((mark + 1))" "$out" 2>/dev/null
}

# irc_expect <name> <extended-regex> [timeout-seconds]
# Polls until the pattern shows up in the post-mark buffer. Returns 0/1.
irc_expect() {
    name="$1"
    pattern="$2"
    timeout="${3:-2.0}"
    iters=$(awk "BEGIN{ n = $timeout / 0.1; printf \"%d\", (n < 1 ? 1 : n) }")
    i=0
    while [ "$i" -lt "$iters" ]; do
        if irc_buf "$name" | grep -qE "$pattern" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
        i=$((i + 1))
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

# irc_register <name> <nick> [password]
# Pass the literal string NOPASS as the third arg to skip PASS entirely.
irc_register() {
    name="$1"
    nick="$2"
    pass="${3-$IRC_PASSWORD}"
    if [ "$pass" != "NOPASS" ]; then
        irc_send "$name" "PASS $pass"
    fi
    irc_send "$name" "NICK $nick"
    irc_send "$name" "USER $nick 0 * :Real Name"
    irc_expect "$name" "(^| )001( |:)|Welcome" 2.0
}

# irc_server_alive — can we still open a fresh TCP connection at all?
irc_server_alive() {
    if (exec 3<>"/dev/tcp/$IRC_HOST/$IRC_PORT") 2>/dev/null; then
        exec 3>&- 3<&- 2>/dev/null
        return 0
    fi
    # Fallback for shells without /dev/tcp (hellish may not implement it).
    if command -v nc >/dev/null 2>&1; then
        printf '' | timeout 2 nc -z "$IRC_HOST" "$IRC_PORT" >/dev/null 2>&1 && return 0
    fi
    return 1
}