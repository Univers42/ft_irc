#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# verify_names.sh — does this server honour the naming rules it advertises?
#
# Three different questions, and the report keeps them apart on purpose:
#
#   PASS     the server behaves as RFC 2812 and its own 005 token say it should
#   FAIL     the server contradicts itself — it advertises one thing and does
#            another. This is a bug.
#   DIVERGE  the server is deliberately stricter (or looser) than RFC 2812.
#            Not a bug, but you should know about it before an evaluator finds
#            it for you.
#
# RFC 2812 §2.3.1:
#   nickname =  ( letter / special ) *8( letter / digit / special / "-" )
#   special  =  %x5B-60 / %x7B-7D    ; "[", "]", "\", "`", "_", "^", "{", "|", "}"
#   channel  =  ( "#" / "+" / "!" ... ) chanstring   ; no space, BEL or comma
#
# Runs against whatever simulation is currently up; every probe is its own
# short-lived connection and quits cleanly, so it never disturbs the cast.
# ---------------------------------------------------------------------------
set -uo pipefail

SIM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SIM_LIB_DIR
# shellcheck disable=SC1090
. "$SIM_LIB_DIR/lib.sh"
sim_load_env || { err "no simulation running"; exit 1; }

N_PASS=0; N_FAIL=0; N_DIV=0

row_pass() { N_PASS=$((N_PASS+1)); printf '  %s%-8s%s %-22s %s\n' "$C_OK"   "PASS"    "$C_RESET" "$1" "$2"; }
row_fail() { N_FAIL=$((N_FAIL+1)); printf '  %s%-8s%s %-22s %s\n' "$C_ERR"  "FAIL"    "$C_RESET" "$1" "$2"; }
row_div()  { N_DIV=$((N_DIV+1));   printf '  %s%-8s%s %-22s %s\n' "$C_WARN" "DIVERGE" "$C_RESET" "$1" "$2"; }
section()  { printf '\n%s%s%s\n' "$C_B" "$1" "$C_RESET"; }

# probe <line> [line...] — one throwaway session, prints everything the server
# said. printf '%s' (never '%b') so a nickname containing a backslash is sent
# as typed instead of being mangled into an escape.
probe() {
    { for _l in "$@"; do printf '%s\r\n' "$_l"; done; sleep 0.6; } \
        | timeout 4 nc -C "$IRC_HOST" "$IRC_PORT" 2>/dev/null
}

reg() { probe "PASS $IRC_PASSWORD" "NICK $1" "USER u 0 * :U" "${@:2}" "QUIT"; }

# --- nicknames the server must accept --------------------------------------

section "Nicknames — RFC 2812 legal, must be accepted"
nick_accept() {  # <nick> [registered-as] [note]
    _want="${2:-$1}"
    if reg "$1" | grep -aqF " 001 $_want "; then
        row_pass "$1" "${3:-registered as $_want}"
    else
        row_fail "$1" "${3:-expected 001 as $_want}"
    fi
}

nick_accept 'z1'                 'z1'  'plain'
nick_accept 'Zed'                'Zed' 'case preserved'
nick_accept 'z'                  'z'   'single letter'
nick_accept 'z9'                 'z9'  'digit after first'
nick_accept 'z-dash'             'z-dash' 'dash after first'
nick_accept '[zbr]'              '[zbr]'  'special: [ ]'
nick_accept '{zbc}'              '{zbc}'  'special: { }'
nick_accept 'z\bs'               'z\bs'   'special: backslash'
nick_accept 'z|pipe'             'z|pipe' 'special: pipe'
nick_accept 'z^car'              'z^car'  'special: caret'
nick_accept 'z_und'              'z_und'  'special: underscore'

# --- nicknames the server must reject --------------------------------------

section "Nicknames — illegal, must answer 432"
nick_reject() {  # <nick> <note>
    if reg "$1" | grep -aqF " 432 "; then
        row_pass "$1" "$2 -> 432"
    else
        row_fail "$1" "$2 -> expected 432"
    fi
}

nick_reject '9lead'   'leading digit'
nick_reject '-lead'   'leading dash'
nick_reject 'a,b'     'comma'
nick_reject '#chan'   'channel prefix'
nick_reject 'a!b'     'bang'
nick_reject 'a@b'     'at sign'
nick_reject 'a.b'     'dot'
nick_reject 'a*b'     'asterisk'
nick_reject 'a?b'     'question mark'
nick_reject 'café'    'non-ASCII'

if probe "PASS $IRC_PASSWORD" "NICK" "QUIT" | grep -aqF " 431 "; then
    row_pass '(no parameter)' 'NICK alone -> 431'
else
    row_fail '(no parameter)' 'NICK alone -> expected 431'
fi

# --- documented divergence from RFC 2812 -----------------------------------

section "Nicknames — RFC 2812 vs this server"
# %x60 (backtick) is inside RFC 2812's `special` range, so a strict reading
# makes it a legal nickname character. Server::isValidNickname enumerates the
# specials by hand and leaves it out.
_bt_out="$(reg 'z`tick')"
if printf '%s' "$_bt_out" | grep -aqF " 001 "; then
    row_pass 'z`tick'  'backtick accepted, as RFC 2812 allows'
elif printf '%s' "$_bt_out" | grep -aqF " 432 "; then
    row_div  'z`tick'  'RFC 2812 allows ` (%x60); this server answers 432'
else
    row_fail 'z`tick'  'neither accepted nor rejected'
fi

# --- NICKLEN ---------------------------------------------------------------

section "Nickname length — 005 says NICKLEN=9"
_adv="$(reg 'z2' | grep -a ' 005 ' | grep -ao 'NICKLEN=[0-9]*' | head -1 | cut -d= -f2)"
if [ "${_adv:-}" = "9" ]; then
    row_pass 'NICKLEN=9' 'advertised in 005'
else
    row_fail 'NICKLEN' "005 advertises '${_adv:-nothing}'"
fi

# A nick of exactly NICKLEN registers; anything past it draws 432. The server
# used to truncate here instead -- see the NickLength note in
# tests/test_conformance.cpp for why that was reversed.
len_ok() {  # <nick> — must register under its own name
    if reg "$1" | grep -aqF " 001 $1 "; then
        row_pass "$1" "${#1} chars -> registers"
    else
        row_fail "$1" "${#1} chars -> expected to register as '$1'"
    fi
}
len_refused() {  # <nick> — must draw 432 and register nothing
    _r="$(reg "$1")"
    if ! printf '%s' "$_r" | grep -aq ' 432 '; then
        row_fail "$1" "${#1} chars -> expected 432 ERR_ERRONEUSNICKNAME"
    elif printf '%s' "$_r" | grep -aq ' 001 '; then
        row_fail "$1" "${#1} chars -> refused but still registered"
    else
        row_pass "$1" "${#1} chars -> 432, nothing registered"
    fi
}
len_ok      'abcdefghi'                 # exactly 9, the bound itself
len_refused 'abcdefghij'                # 10, one over
len_refused 'probeclient'               # 11, the classic

# While the server truncated, two different over-long nicks could shorten onto
# the same name; whether they collided depended on truncation running before
# the in-use check. Refusing removes the hazard -- neither name is created, so
# a 433 here would mean truncation had come back.
_a="$(reg 'trunctestAA')"
if printf '%s' "$_a" | grep -aq ' 433 '; then
    row_fail 'trunctestAA' '433 means an over-long nick was truncated, not refused'
elif printf '%s' "$_a" | grep -aq ' 432 '; then
    row_pass 'trunctestAA' '11 chars -> 432, no truncated name to collide over'
else
    row_fail 'trunctestAA' 'expected 432 ERR_ERRONEUSNICKNAME'
fi

# --- collisions and casemapping --------------------------------------------

section "Collisions — 005 says CASEMAPPING=ascii"
_cm="$(reg 'z3' | grep -a ' 005 ' | grep -ao 'CASEMAPPING=[a-z-]*' | head -1 | cut -d= -f2)"
if [ "${_cm:-}" = "ascii" ]; then
    row_pass 'CASEMAPPING=ascii' 'advertised in 005'
else
    row_fail 'CASEMAPPING' "005 advertises '${_cm:-nothing}'"
fi

# Hold a nick open on one connection while a second tries to take it, spelled
# differently. Without the background session the first would have quit and
# the name would be free.
_hold_fifo="$SIM_DIR/verify_hold.fifo"
rm -f "$_hold_fifo"; mkfifo "$_hold_fifo"
nc -C "$IRC_HOST" "$IRC_PORT" < "$_hold_fifo" > "$SIM_DIR/verify_hold.out" 2>/dev/null &
_hold_nc=$!
sleep infinity > "$_hold_fifo" 2>/dev/null &
_hold_keep=$!
sleep 0.3
{
    printf 'PASS %s\r\n' "$IRC_PASSWORD"
    printf 'NICK casetestx\r\n'
    printf 'USER u 0 * :U\r\n'
} >> "$_hold_fifo"
sleep 0.7

collide() {  # <nick> <note>
    if reg "$1" | grep -aqF " 433 "; then
        row_pass "$1" "$2 -> 433"
    else
        row_fail "$1" "$2 -> expected 433"
    fi
}
collide 'casetestx' 'exact match'
collide 'CASETESTX' 'upper case'
collide 'CaseTestX' 'mixed case'

# Truncation-induced collision. The held nick is exactly 9 characters on
# purpose: truncation always produces exactly NICKLEN characters, so an
# 8-character name could never be collided with this way. This is the check
# that proves truncation happens BEFORE the in-use test — reverse the two and
# 'casetestxZZ' registers alongside the nick it truncates onto.
if reg 'casetestxZZ' | grep -aqF " 433 "; then
    row_pass 'casetestxZZ' 'truncates onto casetestx -> 433'
else
    row_fail 'casetestxZZ' 'truncates onto casetestx -> expected 433'
fi

kill "$_hold_keep" 2>/dev/null; kill "$_hold_nc" 2>/dev/null
rm -f "$_hold_fifo" "$SIM_DIR/verify_hold.out"

# --- channel names ---------------------------------------------------------

section "Channel names — 005 says CHANTYPES=# CHANNELLEN=50"
_ct="$(reg 'z4' | grep -a ' 005 ' | grep -ao 'CHANTYPES=[^ ]*' | head -1 | cut -d= -f2)"
_cl="$(reg 'z4' | grep -a ' 005 ' | grep -ao 'CHANNELLEN=[0-9]*' | head -1 | cut -d= -f2)"
[ "${_ct:-}" = "#" ]  && row_pass 'CHANTYPES=#'    'advertised in 005' \
                      || row_fail 'CHANTYPES'      "005 advertises '${_ct:-nothing}'"
[ "${_cl:-}" = "50" ] && row_pass 'CHANNELLEN=50'  'advertised in 005' \
                      || row_fail 'CHANNELLEN'     "005 advertises '${_cl:-nothing}'"

chan_accept() {  # <channel> <note>
    if probe "PASS $IRC_PASSWORD" "NICK z5" "USER u 0 * :U" "JOIN $1" "QUIT" \
        | grep -aqF " JOIN $1"; then
        row_pass "$1" "$2"
    else
        row_fail "$1" "$2 -> expected a JOIN echo"
    fi
}
chan_reject() {  # <channel> <note>
    if probe "PASS $IRC_PASSWORD" "NICK z6" "USER u 0 * :U" "JOIN $1" "QUIT" \
        | grep -aqE ' (476|403|461) '; then
        row_pass "$1" "$2 -> refused"
    else
        row_fail "$1" "$2 -> expected 476"
    fi
}

_cmax="$(printf  '#%049d' 0)"    # '#' + 49 digits = 50 chars, the maximum
_cover="$(printf '#%050d' 0)"    # '#' + 50 digits = 51 chars, one over
chan_accept '#zz'        'two characters, the minimum'
chan_accept '#zz-dash'   'dash'
chan_accept '#zz_und'    'underscore'
chan_accept "$_cmax"     '50 characters, the maximum'
chan_reject 'zzplain'    'no # prefix'
chan_reject '#'          'prefix only'
chan_reject '&zz'        '& prefix (CHANTYPES=# only)'
chan_reject "$_cover"    '51 characters, one over'

# --- channel casemapping and canonical echo --------------------------------

section "Channels — casemapping and canonical form"
# Create it in mixed case, then join it spelled differently. The server must
# treat them as one channel AND echo its own stored spelling, not the
# caller's — a client that matches its channel list by string desyncs
# otherwise.
_ch_fifo="$SIM_DIR/verify_chan.fifo"
rm -f "$_ch_fifo"; mkfifo "$_ch_fifo"
nc -C "$IRC_HOST" "$IRC_PORT" < "$_ch_fifo" > "$SIM_DIR/verify_chan.out" 2>/dev/null &
_ch_nc=$!
sleep infinity > "$_ch_fifo" 2>/dev/null &
_ch_keep=$!
sleep 0.3
{
    printf 'PASS %s\r\n' "$IRC_PASSWORD"
    printf 'NICK zowner\r\n'
    printf 'USER u 0 * :U\r\n'
    printf 'JOIN #ZzCaseChan\r\n'
} >> "$_ch_fifo"
sleep 0.7

_join_out="$(probe "PASS $IRC_PASSWORD" "NICK z7" "USER u 0 * :U" "JOIN #zzcasechan" "QUIT")"
if printf '%s' "$_join_out" | grep -aqF " JOIN #ZzCaseChan"; then
    row_pass '#zzcasechan' 'same channel, echoed as #ZzCaseChan'
elif printf '%s' "$_join_out" | grep -aqF " JOIN #zzcasechan"; then
    row_fail '#zzcasechan' 'joined but echoed the caller spelling, not the stored one'
else
    row_fail '#zzcasechan' 'did not reach the existing #ZzCaseChan'
fi

if printf '%s' "$_join_out" | grep -aqF "zowner"; then
    row_pass '#zzcasechan' 'sees the creator in the names list'
else
    row_fail '#zzcasechan' 'creator missing from names — separate channels'
fi

kill "$_ch_keep" 2>/dev/null; kill "$_ch_nc" 2>/dev/null
rm -f "$_ch_fifo" "$SIM_DIR/verify_chan.out"

# A comma is a LIST separator, never part of a name.
_multi="$(probe "PASS $IRC_PASSWORD" "NICK z8" "USER u 0 * :U" "JOIN #zma,#zmb" "QUIT")"
if printf '%s' "$_multi" | grep -aqF " JOIN #zma" && \
   printf '%s' "$_multi" | grep -aqF " JOIN #zmb"; then
    row_pass '#zma,#zmb' 'comma splits into two channels'
else
    row_fail '#zma,#zmb' 'expected two separate JOINs'
fi

# --- summary ---------------------------------------------------------------

printf '\n%s%s%s\n' "$C_B" "─────────────────────────────────────────────" "$C_RESET"
printf '  %spass %d%s   %sdiverge %d%s   %sfail %d%s\n\n' \
    "$C_OK" "$N_PASS" "$C_RESET" "$C_WARN" "$N_DIV" "$C_RESET" "$C_ERR" "$N_FAIL" "$C_RESET"

if [ "$N_FAIL" -gt 0 ]; then
    err "$N_FAIL naming check(s) failed — the server contradicts what it advertises"
    exit 1
fi
if [ "$N_DIV" -gt 0 ]; then
    warn "$N_DIV deliberate divergence(s) from RFC 2812 — see the notes above"
fi
ok "naming conventions hold"
