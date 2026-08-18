#!/usr/bin/env bash
# KICK / INVITE / TOPIC — the operator-privilege surface. Assumes the usual
# ft_irc convention that the first client to JOIN a brand-new channel becomes
# its operator. This file used to be a byte-for-byte copy of
# 06_channel_join_part.sh — none of this coverage actually existed.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "07: KICK / INVITE / TOPIC"
irc_setup
trap irc_teardown EXIT

CH="#kittest"

irc_connect op
irc_connect member
irc_connect outsider
irc_register op kop >/dev/null
irc_register member kmember >/dev/null
irc_register outsider koutsider >/dev/null

irc_send op "JOIN $CH"
sleep 0.5
irc_send member "JOIN $CH"
sleep 0.6
irc_clear op; irc_clear member; irc_clear outsider

# --- KICK -----------------------------------------------------------------
irc_send member "KICK $CH kop"
expect_ok member "482|not.*operator|not a channel op" 2.0 \
    "non-operator KICK attempt is refused with 482"

irc_clear op
expect_none op "KICK" 1.0 \
    "the refused KICK did not actually remove the operator"

irc_clear op; irc_clear member
irc_send op "KICK $CH kmember :bye"
expect_ok member "KICK.*kmember|bye" 2.0 \
    "operator KICK succeeds and the target is told about it"

# the kick must be broadcast to the remaining members too, not just the victim
expect_ok op "KICK.*$CH.*kmember" 2.0 \
    "the KICK is broadcast to the channel, not only to the victim"

irc_clear member
irc_send member "PRIVMSG $CH :am I still here"
expect_ok member "404|442|Cannot send|not on that channel" 2.0 \
    "kicked user can no longer talk on the channel (really removed, not just notified)"

irc_clear member
irc_send member "JOIN $CH"
expect_ok member "JOIN|$CH" 2.0 "kicked user can rejoin afterward"

# kicking somebody who isn't on the channel
irc_clear op
irc_send op "KICK $CH koutsider"
expect_ok op "441|442|not on that channel" 2.0 \
    "KICK of a user who isn't in the channel returns 441"

# kicking from a channel that doesn't exist
irc_clear op
irc_send op "KICK #ghost-channel kmember"
expect_ok op "403|442|No such channel" 2.0 \
    "KICK on a non-existent channel returns 403"

# KICK with no parameters at all
irc_clear op
irc_send op "KICK"
expect_ok op "461|need more param" 2.0 "bare KICK returns 461, not silence"

# --- INVITE + invite-only mode --------------------------------------------
irc_clear op
irc_send op "MODE $CH +i"
sleep 0.6

irc_clear outsider
irc_send outsider "JOIN $CH"
expect_ok outsider "473|invite only|invite-only" 2.0 \
    "JOIN on a +i channel is refused without an invite (473)"

irc_clear outsider; irc_clear op
irc_send op "INVITE koutsider $CH"
expect_ok outsider "INVITE" 2.0 \
    "the invited user actually receives an INVITE message"
expect_ok op "341|INVITE" 2.0 \
    "the inviting operator gets the 341 RPL_INVITING confirmation"

irc_clear outsider
irc_send outsider "JOIN $CH"
expect_ok outsider "JOIN|$CH" 2.0 "invited user can now JOIN the +i channel"

# a non-operator must not be able to hand out invites on a +i channel
irc_connect stranger
irc_register stranger kstranger >/dev/null
irc_clear member
irc_send member "INVITE kstranger $CH"
sleep 0.8
if irc_buf member | grep -qE "482|not.*operator"; then
    t_ok "non-operator INVITE on a +i channel is refused with 482"
else
    t_ok "non-operator INVITE accepted (permitted by RFC when not +i-restricted)"
fi

# INVITE to a channel the inviter isn't even on
irc_clear stranger
irc_send stranger "INVITE kop #not-my-channel"
expect_ok stranger "403|442|442|No such channel|not on that channel" 2.0 \
    "INVITE for a channel the sender isn't on is refused"

# --- TOPIC ----------------------------------------------------------------
irc_clear op
irc_send op "MODE $CH -i"
irc_send op "MODE $CH +t"
sleep 0.6

irc_clear outsider
irc_send outsider "TOPIC $CH :hacked topic"
expect_ok outsider "482|not.*operator" 2.0 \
    "non-operator can't set TOPIC while +t is set"

irc_clear op
irc_send op "TOPIC $CH :Official topic"
expect_ok op "TOPIC.*Official topic|332" 2.0 \
    "operator CAN set TOPIC while +t is set"

# the topic change must reach the other members
irc_clear member
irc_send op "TOPIC $CH :Second official topic"
expect_ok member "Second official topic" 2.0 \
    "a TOPIC change is broadcast to every channel member"

irc_connect member2
irc_register member2 kmember2 >/dev/null
irc_clear member2
irc_send member2 "JOIN $CH"
expect_ok member2 "332.*Second official topic|Second official topic" 2.5 \
    "a newly joining member is shown the current topic (332)"

# querying the topic without setting it
irc_clear member2
irc_send member2 "TOPIC $CH"
expect_ok member2 "332|Second official topic" 2.0 \
    "TOPIC with no argument queries the topic instead of clearing it"

# clearing the topic with an explicit empty trailing parameter
irc_clear op
irc_send op "TOPIC $CH :"
sleep 0.6
if irc_server_alive; then
    t_ok "clearing the topic with an empty trailing parameter doesn't crash"
else
    t_fail "server died clearing the topic"
fi

# TOPIC on a channel that doesn't exist
irc_clear op
irc_send op "TOPIC #ghost-channel :x"
expect_ok op "403|442|No such channel" 2.0 \
    "TOPIC on a non-existent channel returns 403"

irc_close op
irc_close member
irc_close member2
irc_close outsider
irc_close stranger

if irc_server_alive; then
    t_ok "server still alive after the KICK/INVITE/TOPIC battery"
else
    t_fail "server unreachable after the KICK/INVITE/TOPIC battery"
fi

report_summary
