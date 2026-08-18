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

report_summary