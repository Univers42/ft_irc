#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 00_shell_probe.sh — gate for the rest of the suite.
#
# irc_lib.sh promises it uses "no bashisms beyond what tests/00_shell_probe.sh
# explicitly checks". This file is that check: it exercises exactly the shell
# features the library depends on, so that when a test fails under an
# alternative shell you can tell a *server* bug from a *shell* bug.
#
# Output is deterministic on purpose — run it under two shells and diff.
#   bash    tests/00_shell_probe.sh
#   hellish tests/00_shell_probe.sh
# ---------------------------------------------------------------------------
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "00: shell capability probe"

PROBE_TMP="${IRC_TMPDIR:-/tmp/ftirc_probe_$$}_probe"
mkdir -p "$PROBE_TMP" || { echo "cannot create $PROBE_TMP"; exit 1; }
cleanup_probe() { rm -rf "$PROBE_TMP"; }
trap cleanup_probe EXIT

# probe <description> <expected> <actual>
probe() {
    if [ "$2" = "$3" ]; then
        t_ok "$1"
    else
        t_fail "$1 (expected [$2], got [$3])"
    fi
}

# --- parameter expansion the library relies on ----------------------------
s=hello
probe "\${#var} string length"            "5"     "${#s}"
probe "\${var:-default} fallback"         "fb"    "${unset_on_purpose:-fb}"
probe "\${var:+alt} when set"             "yes"   "${s:+yes}"
probe "\${var%suffix} strip"              "hel"   "${s%lo}"
probe "\${var#prefix} strip"              "llo"   "${s#he}"

# --- arithmetic -----------------------------------------------------------
i=0
i=$((i + 1))
i=$((i * 7))
probe "\$((...)) arithmetic"              "7"     "$i"

# --- command substitution -------------------------------------------------
probe "\$(...) command substitution"      "sub"   "$(echo sub)"
probe "nested command substitution"       "deep"  "$(echo "$(echo deep)")"

# --- printf, the only output primitive the library uses -------------------
probe "printf %s"                         "abc"   "$(printf '%s' abc)"
probe "printf %b interprets escapes"      "a	b"  "$(printf '%b' 'a\tb')"
probe "printf %d"                         "42"    "$(printf '%d' 42)"
crlf=$(printf 'x\r\n' | wc -c | tr -d ' ')
probe "printf emits CR and LF as 3 bytes" "3"     "$crlf"

# --- functions, return values, positional args ----------------------------
probe_fn() { echo "$1-$2-$#"; }
probe "function args and \$#"             "a-b-2" "$(probe_fn a b)"
rc_fn() { return 3; }
rc_fn
probe "function return status"            "3"     "$?"
local_fn() { local lv=inner; echo "$lv"; }
probe "local variables inside functions"  "inner" "$(local_fn)"

# --- eval + dynamically named variables -----------------------------------
# This is how irc_lib.sh stores per-client state (IRC_FD_<name> etc.). If this
# breaks, every single test breaks.
n=widget
eval "IRC_PROBE_$n=99"
eval "got=\$IRC_PROBE_$n"
probe "eval with dynamically named variables" "99" "$got"

# --- redirection and file descriptors -------------------------------------
echo body > "$PROBE_TMP/f"
probe "> redirect and read back"          "body"  "$(cat "$PROBE_TMP/f")"
echo more >> "$PROBE_TMP/f"
probe ">> append"                         "2"     "$(wc -l < "$PROBE_TMP/f" | tr -d ' ')"

# a numbered fd held open across commands — the core of irc_send
exec 7> "$PROBE_TMP/fd7"
printf 'first\n' >&7
printf 'second\n' >&7
exec 7>&-
probe "exec N> holds an fd open across commands" "2" \
      "$(wc -l < "$PROBE_TMP/fd7" | tr -d ' ')"

# the same, but with the fd number in a variable — irc_send does exactly this
exec 8> "$PROBE_TMP/fd8"
fdvar=8
printf 'viavar\n' >&"$fdvar"
exec 8>&-
probe "redirect to an fd held in a variable" "viavar" "$(cat "$PROBE_TMP/fd8")"

# --- FIFOs: how every client's stdin is fed -------------------------------
if command -v mkfifo >/dev/null 2>&1; then
    mkfifo "$PROBE_TMP/fifo" 2>/dev/null
    if [ -p "$PROBE_TMP/fifo" ]; then
        t_ok "mkfifo creates a named pipe"
        # Reader first, then the writer fd — opening a FIFO for writing
        # blocks until a reader exists. irc_connect() does it in this order
        # too (backgrounded nc, then `exec $fd> $fifo`).
        cat < "$PROBE_TMP/fifo" > "$PROBE_TMP/fifo.out" &
        fifo_reader=$!
        exec 9> "$PROBE_TMP/fifo"
        printf 'through-the-fifo\n' >&9
        sleep 0.3
        exec 9>&-
        wait "$fifo_reader" 2>/dev/null
        probe "writer fd keeps a FIFO open without EOF" \
              "through-the-fifo" "$(cat "$PROBE_TMP/fifo.out")"
    else
        t_fail "mkfifo did not produce a FIFO"
    fi
else
    t_fail "mkfifo not available — the whole suite depends on it"
fi

# --- background jobs and signals ------------------------------------------
sleep 5 &
bgpid=$!
if [ -n "$bgpid" ]; then
    t_ok "\$! reports the background job's pid"
else
    t_fail "\$! did not report a background pid"
fi
kill -STOP "$bgpid" 2>/dev/null && t_ok "kill -STOP (used by irc_freeze)" \
                                 || t_fail "kill -STOP failed"
kill -CONT "$bgpid" 2>/dev/null && t_ok "kill -CONT (used by irc_thaw)" \
                                 || t_fail "kill -CONT failed"
kill -9 "$bgpid" 2>/dev/null && t_ok "kill -9 (used by irc_kill_hard)" \
                             || t_fail "kill -9 failed"
wait "$bgpid" 2>/dev/null

# --- loops and conditionals -----------------------------------------------
acc=""
j=1
while [ "$j" -le 3 ]; do
    acc="$acc$j"
    j=$((j + 1))
done
probe "while loop with [ ] test"          "123"   "$acc"

acc=""
for w in a b c; do acc="$acc$w"; done
probe "for-in loop"                       "abc"   "$acc"

case_result=no
case "#channel" in
    '#'*) case_result=yes ;;
esac
probe "case with a glob pattern"          "yes"   "$case_result"

# --- external tools the library shells out to -----------------------------
for tool in nc grep sed awk tr cut tail wc head kill sleep date; do
    if command -v "$tool" >/dev/null 2>&1; then
        t_ok "external tool present: $tool"
    else
        t_fail "external tool MISSING: $tool"
    fi
done

# awk float arithmetic — irc_expect computes its poll count with it
probe "awk float arithmetic for timeouts" "20" \
      "$(awk 'BEGIN{ n = 2.0 / 0.1; printf "%d", n }')"

# sub-second sleep — every poll loop depends on it
t0=$(date +%s)
sleep 0.1
t1=$(date +%s)
if [ $((t1 - t0)) -le 1 ]; then
    t_ok "sleep accepts a fractional argument"
else
    t_fail "sleep 0.1 took more than a second — poll loops will be very slow"
fi

# --- trap -----------------------------------------------------------------
trap_out=$( (trap 'echo trapped' EXIT; echo body) )
probe "trap ... EXIT fires at subshell exit" "body
trapped" "$trap_out"

# --- export ---------------------------------------------------------------
# Deliberately one name per export: `export A B` is mis-parsed by some shells
# (it consumes the following name as a value). config.sh does the same.
EXP_A=1
EXP_B=2
export EXP_A
export EXP_B
probe "export preserves values (one name per export)" "1|2" \
      "$(sh -c 'printf "%s|%s" "$EXP_A" "$EXP_B"')"

report_summary
