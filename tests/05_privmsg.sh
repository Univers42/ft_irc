#!/usr/bin/env bash
# PRIVMSG / NOTICE routing: user-to-user, channel fan-out, and every way the
# target can be wrong. This file used to be a byte-for-byte copy of
# 04_disconnect.sh — the PRIVMSG coverage it claimed never existed.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "05: PRIVMSG / NOTICE"
irc_setup
trap irc_teardown EXIT

irc_connect alice
irc_connect bob
irc_register alice palice >/dev/null
irc_register bob pbob >/dev/null
irc_clear alice
irc_clear bob

# --- 1. user -> user delivery ---------------------------------------------
irc_send alice "PRIVMSG pbob :hello there"
expect_ok bob "hello there" 2.0 "PM from alice reaches bob's socket"

# the sender must NOT be echoed his own PM (a classic fan-out bug)
expect_none alice "hello there" 1.0 \
    "sender does not receive an echo of his own PRIVMSG"

# --- 2. the prefix must identify the sender -------------------------------
irc_clear bob
irc_send alice "PRIVMSG pbob :prefix check"
if irc_expect bob ":palice.*PRIVMSG.*pbob.*prefix check" 2.0; then
    t_ok "delivered PM carries a :nick!user@host PRIVMSG prefix"
else
    t_fail "delivered PM is missing the sender prefix (got: $(irc_buf bob | tr -d '\r' | tail -1))"
fi

# --- 3. unknown nick ------------------------------------------------------
irc_clear alice
irc_send alice "PRIVMSG nobody-here :hi"
expect_ok alice "401|No such nick" 2.0 \
    "PRIVMSG to an unknown nick returns 401"

# --- 4. missing target ----------------------------------------------------
irc_clear alice
irc_send alice "PRIVMSG"
expect_ok alice "411|461|No recipient|need more param" 2.0 \
    "PRIVMSG with no target errors (411/461)"

# --- 5. target but no text ------------------------------------------------
irc_clear alice
irc_send alice "PRIVMSG pbob"
expect_ok alice "412|461|No text to send" 2.0 \
    "PRIVMSG with a target but no message body errors (412/461)"

# --- 6. channel delivery --------------------------------------------------
irc_send alice "JOIN #pmtest"
irc_send bob "JOIN #pmtest"
sleep 0.6
irc_clear alice
irc_clear bob
irc_send alice "PRIVMSG #pmtest :channel hello"
expect_ok bob "channel hello" 2.0 "channel PRIVMSG reaches the other member"
expect_none alice "channel hello" 1.0 \
    "channel PRIVMSG is not echoed back to the speaker"

# --- 7. channel you are not a member of -----------------------------------
irc_connect carol
irc_register carol pcarol >/dev/null
irc_clear carol
irc_send carol "PRIVMSG #pmtest :sneaky"
expect_ok carol "404|442|Cannot send|not on that channel" 2.0 \
    "PRIVMSG to a channel you haven't joined is rejected"
irc_clear bob
expect_none bob "sneaky" 1.0 \
    "the rejected outsider message never reached the channel members"

# --- 8. non-existent channel ----------------------------------------------
irc_clear carol
irc_send carol "PRIVMSG #no-such-channel-at-all :hello"
expect_ok carol "401|403|404|No such" 2.0 \
    "PRIVMSG to a channel that doesn't exist errors instead of being dropped"

# --- 9. NOTICE must never generate an error reply -------------------------
# RFC 2812 §3.3.2: a NOTICE must never trigger an automatic reply, because
# that is how two servers/bots end up in an infinite error loop.
irc_clear carol
irc_send carol "NOTICE nobody-here :quiet"
expect_none carol "401|No such nick" 1.5 \
    "NOTICE to an unknown nick produces NO error reply (RFC 2812 3.3.2)"

irc_clear bob
irc_send alice "NOTICE pbob :notice body"
expect_ok bob "notice body" 2.0 "NOTICE to a valid nick is still delivered"

# --- 10. message text edge cases ------------------------------------------
irc_clear bob
irc_send alice "PRIVMSG pbob ::leading-colon-text"
expect_ok bob ":leading-colon-text" 2.0 \
    "message body beginning with ':' survives trailing-parameter parsing"

irc_clear bob
irc_send alice "PRIVMSG pbob :   spaced   out   "
expect_ok bob "spaced   out" 2.0 \
    "internal runs of spaces in the trailing parameter are preserved"

irc_clear bob
irc_send alice "PRIVMSG pbob :"
sleep 0.5
if irc_server_alive; then
    t_ok "empty trailing parameter doesn't take the server down"
else
    t_fail "server died on an empty trailing parameter"
fi

# --- 11. case-insensitive nick routing ------------------------------------
# RFC 2812 §2.2: nicknames are case-insensitive.
irc_clear bob
irc_send alice "PRIVMSG PBOB :case insensitive routing"
expect_ok bob "case insensitive routing" 2.0 \
    "nick targets are matched case-insensitively (RFC 2812 2.2)"

# --- 12. a long body must not be truncated into a second command ----------
irc_clear bob
longbody=$(head -c 400 /dev/zero | tr '\0' 'L')
irc_send alice "PRIVMSG pbob :$longbody"
sleep 0.8
if irc_buf bob | grep -qE "LLLLLLLLLL"; then
    t_ok "a 400-byte message body is delivered"
else
    t_fail "a 400-byte message body was dropped entirely"
fi
if irc_buf bob | grep -qiE "^(421|Unknown command)"; then
    t_fail "long body leaked into the parser as a second command"
else
    t_ok "long body did not leak into the parser as a second command"
fi

# --- 13. self-messaging ---------------------------------------------------
irc_clear alice
irc_send alice "PRIVMSG palice :note to self"
expect_ok alice "note to self" 2.0 "a client can PRIVMSG itself"

irc_close alice
irc_close bob
irc_close carol

if irc_server_alive; then
    t_ok "server still alive after the full PRIVMSG battery"
else
    t_fail "server unreachable after the PRIVMSG battery"
fi

report_summary
