#!/usr/bin/env bash
# Checks that don't need a running server at all: does it build cleanly,
# and does it respect the mandatory-subject constraints (no fork/exec/threads,
# exactly one poll-family call, C++98 only).
set -uo pipefail
cd "$(dirname "$0")/.." && source config.sh

PASS=0
FAIL=0
ok()   { PASS=$((PASS+1)); echo "  [PASS] $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  [FAIL] $1"; }

echo "=== 00: build & norm ==="

if [[ ! -d "$PROJECT_DIR/src" ]]; then
    echo "  [SKIP] $PROJECT_DIR/src not found — set PROJECT_DIR to your ft_irc root"
    exit 0
fi
cd "$PROJECT_DIR"

# --- Makefile targets -------------------------------------------------------
for target in "" re clean fclean; do
    label="make ${target:-<default>}"
    if make -s ${target} >/tmp/ftirc_make.log 2>&1; then
        ok "$label succeeds"
    else
        bad "$label failed — see /tmp/ftirc_make.log"
    fi
done

# rebuild for the rest of the checks since fclean just wiped the binary
make -s re >/tmp/ftirc_make.log 2>&1 || bad "final rebuild after fclean failed"
[[ -x "$BIN" ]] && ok "binary exists and is executable at $BIN" || bad "no executable found at $BIN"

# --- compiler warnings -------------------------------------------------------
if grep -qE '\-Wall' Makefile 2>/dev/null && grep -qE '\-Wextra' Makefile 2>/dev/null; then
    ok "Makefile requests -Wall -Wextra"
else
    bad "Makefile is missing -Wall and/or -Wextra"
fi
if make -s re 2>&1 | grep -iE 'warning:' >/tmp/ftirc_warnings.log; then
    bad "compilation produced warnings — see /tmp/ftirc_warnings.log"
else
    ok "clean rebuild, zero warnings"
fi

# --- C++98 -------------------------------------------------------
if grep -qE 'std=c\+\+98' Makefile 2>/dev/null; then
    ok "Makefile pins -std=c++98"
else
    bad "Makefile does not pin -std=c++98 (check it isn't silently using a newer standard)"
fi

# --- forbidden functions -------------------------------------------------------
for fn in fork execl execve execlp execvp pthread_create; do
    hits=$(grep -RnwE "$fn" src/ 2>/dev/null | grep -v '^Binary')
    if [[ -z "$hits" ]]; then
        ok "no use of $fn"
    else
        bad "forbidden call to $fn found:"
        echo "$hits" | sed 's/^/           /'
    fi
done

# --- exactly one poll/select/epoll_wait/kevent call -------------------------------------------------------
matches=0
total=0
for f in poll select epoll_wait kevent; do
    count=$(grep -RhoE "\b${f}\s*\(" src/ 2>/dev/null | wc -l)
    if (( count > 0 )); then
        matches=$((matches+1))
        total=$((total+count))
        if (( count == 1 )); then
            ok "$f() called exactly once"
        else
            bad "$f() called $count times — should be exactly one call, total, across the codebase"
            grep -RnE "\b${f}\s*\(" src/ | sed 's/^/           /'
        fi
    fi
done
if (( matches != 1 )); then
    bad "expected exactly ONE of poll/select/epoll_wait/kevent to be used, found $matches distinct mechanisms"
elif (( total != 1 )); then
    bad "expected the chosen poll mechanism to be called exactly once total, found $total calls"
fi

echo "--- 00: build & norm: $PASS/$((PASS+FAIL)) passed ---"
[[ $FAIL -eq 0 ]]
