#!/usr/bin/env bash
# Checks that need no running server: does it build cleanly, and does it
# respect the subject's mandatory constraints (C++98, no fork/exec/threads,
# exactly one poll-family call site)?
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "12: build & norm"

if [ ! -d "$PROJECT_DIR/src" ]; then
    printf '  [SKIP] %s/src not found — set PROJECT_DIR\n' "$PROJECT_DIR"
    exit 0
fi
cd "$PROJECT_DIR" || exit 1

MAKELOG=/tmp/ftirc_make.$$

# --- Makefile targets -----------------------------------------------------
for target in all re clean fclean; do
    if make -s "$target" >"$MAKELOG" 2>&1; then
        t_ok "make $target succeeds"
    else
        t_fail "make $target failed — see $MAKELOG"
    fi
done

# fclean just removed the binary; rebuild for the remaining checks.
if make -s re >"$MAKELOG" 2>&1; then
    t_ok "rebuild after fclean succeeds"
else
    t_fail "rebuild after fclean failed — see $MAKELOG"
fi

if [ -x "$BIN" ]; then
    t_ok "binary exists and is executable at $BIN"
else
    t_fail "no executable found at $BIN"
fi

# --- required compiler flags ---------------------------------------------
for flag in -Wall -Wextra -Werror; do
    if grep -qE -- "$flag" Makefile 2>/dev/null; then
        t_ok "Makefile requests $flag"
    else
        t_fail "Makefile is missing $flag"
    fi
done

if grep -qE -- 'std=c\+\+98' Makefile 2>/dev/null; then
    t_ok "Makefile pins -std=c++98"
else
    t_fail "Makefile does not pin -std=c++98"
fi

# --- a clean rebuild must be warning-free --------------------------------
make -s fclean >/dev/null 2>&1
if make -s re 2>&1 | grep -iE 'warning:' > /tmp/ftirc_warnings.$$; then
    t_fail "compilation produced warnings — see /tmp/ftirc_warnings.$$"
else
    t_ok "clean rebuild, zero warnings"
fi

# --- source scanning helpers ----------------------------------------------
# Diagnostics like  throw std::runtime_error("epoll_wait() failed: " ...)
# contain the very tokens we are counting, so every scan below runs against
# source with // comments and double-quoted string literals removed. Line
# numbering is preserved (sed edits in place, line for line) so the reported
# file:line still points at real code.
strip_code() {
    sed -e 's|//.*$||' -e 's|"[^"]*"||g' "$1"
}

# grep_code <extended-regex> — matches across src/, ignoring comments and
# string literals. Prints file:line:text.
grep_code() {
    find src \( -name '*.cpp' -o -name '*.hpp' \) -print | while read -r _f; do
        strip_code "$_f" | grep -nE "$1" | sed "s|^|$_f:|"
    done
}

# --- forbidden functions --------------------------------------------------

for fn in fork vfork execl execle execlp execv execve execvp system pthread_create clone; do
    hits=""
    for f in $(find src -name '*.cpp' -o -name '*.hpp' 2>/dev/null); do
        if strip_code "$f" | grep -qnwE "$fn" 2>/dev/null; then
            hits="$hits $f"
        fi
    done
    if [ -z "$hits" ]; then
        t_ok "no use of $fn()"
    else
        t_fail "forbidden call to $fn() found in:$hits"
    fi
done

# --- exactly one poll/select/epoll_wait/kevent call ----------------------
matches=0
total=0
for f in poll select epoll_wait kevent; do
    count=$(grep_code "\b${f}[[:space:]]*\(" | wc -l | tr -d ' ')
    if [ "$count" -gt 0 ]; then
        matches=$((matches + 1))
        total=$((total + count))
        if [ "$count" -eq 1 ]; then
            t_ok "$f() called exactly once"
        else
            t_fail "$f() called $count times — must be exactly one call site"
            grep_code "\b${f}[[:space:]]*\(" | sed 's/^/           /'
        fi
    fi
done

if [ "$matches" -ne 1 ]; then
    t_fail "expected exactly ONE of poll/select/epoll_wait/kevent, found $matches distinct mechanisms"
else
    t_ok "exactly one poll mechanism is used"
fi
if [ "$total" -ne 1 ]; then
    t_fail "expected the poll mechanism to be called once total, found $total call sites"
else
    t_ok "the poll mechanism has exactly one call site"
fi

# --- fcntl must only ever be fcntl(fd, F_SETFL, O_NONBLOCK) --------------
fcntl_all=$(grep_code "\bfcntl[[:space:]]*\(" | wc -l | tr -d ' ')
fcntl_ok=$(grep_code "\bfcntl[[:space:]]*\([^,]+,[[:space:]]*F_SETFL[[:space:]]*,[[:space:]]*O_NONBLOCK[[:space:]]*\)" | wc -l | tr -d ' ')
if [ "$fcntl_all" -eq 0 ]; then
    t_ok "fcntl() is not used at all"
elif [ "$fcntl_all" -eq "$fcntl_ok" ]; then
    t_ok "all $fcntl_all fcntl() call(s) use the allowed F_SETFL/O_NONBLOCK form"
else
    t_fail "$((fcntl_all - fcntl_ok)) fcntl() call(s) use a form other than fcntl(fd, F_SETFL, O_NONBLOCK)"
    grep_code "\bfcntl[[:space:]]*\(" | sed 's/^/           /'
fi

rm -f "$MAKELOG"
report_summary
