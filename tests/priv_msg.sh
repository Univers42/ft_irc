#!/usr/bin/env bash
# Abrupt disconnects (kill -9 the nc, no QUIT) must not leave stale state.
cd "$(dirname "$0")/.." || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "04: disconnect cleanup"
irc_setup
trap irc_teardown EXIT

# 1. nickname is freed after an abrupt disconnect
irc_connect ghost
t_assert "$(irc_register ghost ghostnick; echo $?)" "ghost registers"
irc_kill_hard ghost

irc_connect ghost2
if irc_register ghost2 ghostnick; then
    t_ok "nickname is reusable after the original holder vanished"
else
    t_fail "nickname still locked after the original holder vanished"
fi
irc_close ghost2

# 2. remaining channel member is told when a peer disappears
irc_connect observer
irc_register observer observer >/dev/null
irc_send observer "JOIN #cleanup"
sleep 0.4
irc_clear observer

irc_connect leaver
irc_register leaver leaver >/dev/null
irc_send leaver "JOIN #cleanup"
sleep 0.5
irc_clear observer

irc_kill_hard leaver
expect_ok observer "QUIT|PART|leaver" 3.0 \
    "remaining channel member is notified when the other client vanishes"

# 3. a third client can immediately claim the freed nick — proves the server
#    tore down the client object, not just the fd
irc_connect newcomer
if irc_register newcomer leaver; then
    t_ok "the freed nickname is claimable by a brand-new client"
else
    t_fail "the freed nickname is still held after an abrupt disconnect"
fi
irc_close newcomer
irc_close observer

# 4. disconnecting mid-command (no trailing CRLF ever sent)
irc_connect midway
irc_send midway "PASS $IRC_PASSWORD"
irc_send_raw midway "NICK partia"
irc_kill_hard midway
sleep 0.3
if irc_server_alive; then
    t_ok "server survives a client disconnecting mid-command"
else
    t_fail "server died on a client disconnecting mid-command"
fi

report_summary