#!/usr/bin/env bash
# PASS / NICK / USER handshake, and the ways it should refuse to complete.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "02: registration"
irc_setup
trap irc_teardown EXIT

# 1. happy path
irc_connect alice
if irc_register alice alice; then
    t_ok "correct PASS/NICK/USER registers successfully"
else
    t_fail "correct PASS/NICK/USER registers successfully"
fi
irc_close alice

# 2. wrong password
irc_connect bob
if irc_register bob bob "definitely-wrong-password"; then
    t_fail "wrong password does NOT register the client"
else
    t_ok "wrong password does NOT register the client"
fi
irc_close bob

# 3. no password sent at all
irc_connect carol
if irc_register carol carol NOPASS; then
    t_fail "skipping PASS entirely does NOT register the client"
else
    t_ok "skipping PASS entirely does NOT register the client"
fi
irc_close carol

# 4. duplicate nickname
irc_connect dave1
t_assert "$(irc_register dave1 dave; echo $?)" "first client claims nick 'dave'"
irc_connect dave2
irc_send dave2 "PASS $IRC_PASSWORD"
irc_send dave2 "NICK dave"
irc_send dave2 "USER dave2 0 * :Dave Two"
expect_ok dave2 "433|already in use|[Nn]ickname is already" 2.0 \
    "second client using the same nick gets a nickname-in-use style error"
irc_close dave1
irc_close dave2

# 5. bare NICK with no argument
irc_connect badnick
irc_send badnick "PASS $IRC_PASSWORD"
irc_send badnick "NICK"
irc_send badnick "USER u 0 * :U"
expect_ok badnick "431|461|No nickname" 2.0 \
    "bare NICK with no argument is rejected, not silently accepted"
irc_close badnick

# 6. very long nickname doesn't take the server down
irc_connect longnick
longnames=$(head -c 500 /dev/zero | tr '\0' 'A')
irc_send longnick "PASS $IRC_PASSWORD"
irc_send longnick "NICK $longnames"
irc_send longnick "USER u 0 * :U"
sleep 1
if irc_server_alive; then
    t_ok "server still reachable after a 500-character nickname"
else
    t_fail "server unreachable after a 500-character nickname"
fi
irc_close longnick

# 7. re-registering after already registered
irc_connect eve
t_assert "$(irc_register eve eve; echo $?)" "eve registers"
irc_clear eve
irc_send eve "PASS $IRC_PASSWORD"
expect_ok eve "462|[Aa]lready registered" 1.5 \
    "PASS after already-registered is rejected, not silently accepted"
irc_close eve

# 8. NICK change after registration
irc_connect frank
t_assert "$(irc_register frank frank; echo $?)" "frank registers"
irc_clear frank
irc_send frank "NICK franky"
sleep 1
if irc_buf frank | grep -qE "franky"; then
    t_ok "NICK change after registration is acknowledged"
elif irc_buf frank | grep -qiE "^ERROR"; then
    t_fail "NICK change after registration returned a hard ERROR"
else
    t_ok "NICK change after registration didn't error out"
fi
irc_close frank

# --- USER: the four-parameter form -----------------------------------------
# signatures.md: USER <user> <mode> <unused> :<realname>. Exactly four
# parameters, the fourth of which must be the TRAILING one -- the colon is
# part of the grammar. Everything else is 461.
_u=0
try_user() {
    # try_user <user-line-tail> <expect: ok|461> <description>
    _u=$((_u + 1))
    _n="uf$_u"
    irc_connect "$_n"
    irc_send "$_n" "PASS $IRC_PASSWORD"
    irc_send "$_n" "NICK $_n"
    sleep 0.2
    irc_clear "$_n"
    irc_send "$_n" "USER $1"
    sleep 0.6
    if [ "$2" = "ok" ]; then
        expect_ok "$_n" "001|Welcome" 1.5 "$3"
    else
        expect_ok "$_n" "461" 1.5 "$3"
    fi
    irc_close "$_n"
}

try_user "u 0 * :Real Name"  ok  "the canonical four-parameter form registers"
try_user "u 0 * :"           ok  "an empty trailing realname is still a trailing one"
try_user "u 0 * Real"        461 "four parameters but no trailing colon is refused"
try_user "u 0 * Real Name"   461 "...and a multi-word realname without the colon too"
try_user "u 0 * x :y"        461 "five parameters: the trailing is not the realname slot"
try_user "u 0 *"             461 "three parameters: the realname is missing"
try_user "u 0"               461 "two parameters"
try_user "u"                 461 "one parameter"

# --- USER: <user> follows the RFC 2812 'user' production -------------------
# 'user' excludes '@' (0x40). It has to: the prefix is nick!user@host, so an
# '@' inside the username makes the host boundary ambiguous.
try_user "a:b 0 * :R"        ok  "':' is inside the user production and allowed"
try_user "~alice 0 * :R"     ok  "'~' likewise"
try_user "a@b 0 * :R"        461 "'@' is excluded -- it would fork the prefix"
try_user "@lead 0 * :R"      461 "...at the start too"
try_user "trail@ 0 * :R"     461 "...and at the end"

# --- USER: <mode> is a bitmask ---------------------------------------------
# RFC 2812 s3.1.3: bit 2 (value 4) sets user mode w, bit 3 (value 8) sets i.
# A non-numeric mode carries no bits and is ignored, never refused.
check_umode() {
    # check_umode <mode-value> <expected 221 modes> <description>
    _u=$((_u + 1))
    _n="um$_u"
    irc_connect "$_n"
    irc_register "$_n" "$_n" >/dev/null
    irc_clear "$_n"
    irc_send "$_n" "MODE $_n"
    expect_ok "$_n" "221 $_n \\$2" 1.5 "$3"
    irc_close "$_n"
}

_u=$((_u + 1)); _n="bm$_u"
irc_connect "$_n"
irc_send "$_n" "PASS $IRC_PASSWORD"; irc_send "$_n" "NICK $_n"
sleep 0.2; irc_send "$_n" "USER u 12 * :R"; sleep 0.6
irc_clear "$_n"; irc_send "$_n" "MODE $_n"
expect_ok "$_n" "221 $_n \+iw" 1.5 "USER <mode>=12 sets both +i and +w"
irc_close "$_n"

_u=$((_u + 1)); _n="bm$_u"
irc_connect "$_n"
irc_send "$_n" "PASS $IRC_PASSWORD"; irc_send "$_n" "NICK $_n"
sleep 0.2; irc_send "$_n" "USER u 8 * :R"; sleep 0.6
irc_clear "$_n"; irc_send "$_n" "MODE $_n"
expect_ok "$_n" "221 $_n \+i" 1.5 "USER <mode>=8 sets +i only"
irc_close "$_n"

_u=$((_u + 1)); _n="bm$_u"
irc_connect "$_n"
irc_send "$_n" "PASS $IRC_PASSWORD"; irc_send "$_n" "NICK $_n"
sleep 0.2; irc_send "$_n" "USER u abc * :R"; sleep 0.6
irc_clear "$_n"; irc_send "$_n" "MODE $_n"
expect_ok "$_n" "221 $_n \+" 1.5 "a non-numeric <mode> is ignored, not refused"
expect_none "$_n" "221 $_n \+[iw]" 0.5 "...and sets no user mode bits"
irc_close "$_n"

# --- the prefix keeps exactly one '@' --------------------------------------
_u=$((_u + 1)); _n="pfx$_u"
irc_connect "$_n"
irc_register "$_n" "$_n" >/dev/null
if [ "$(irc_buf "$_n" | grep -c '001.*@.*@')" -eq 0 ]; then
    t_ok "the welcome prefix carries exactly one '@'"
else
    t_fail "the welcome prefix has more than one '@' -- the host boundary is ambiguous"
fi
irc_close "$_n"

report_summary
