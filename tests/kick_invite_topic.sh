#!/usr/bin/env bash
cd "$(dirname "$0")/.." || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "06: JOIN / PART"
irc_setup
trap irc_teardown EXIT

irc_connect alice
irc_connect bob
irc_register alice jalice >/dev/null
irc_register bob jbob >/dev/null
irc_clear alice
irc_clear bob

# 1. create/join a new channel
irc_send alice "JOIN #jptest"
expect_ok alice "JOIN.*#jptest|#jptest" 2.0 "creating/joining a new channel is acknowledged"

# 2. existing member sees the second joiner
irc_clear alice
irc_send bob "JOIN #jptest"
expect_ok alice "JOIN.*#jptest|jbob" 2.0 "existing member is notified when someone else joins"

# 3. re-JOIN a channel you're already in
irc_clear alice
irc_send alice "JOIN #jptest"
sleep 1
if irc_buf alice | grep -qiE "^ERROR"; then
    t_fail "re-JOINing a channel you're already in produced a hard ERROR"
else
    t_ok "re-JOINing a channel you're already in doesn't produce a hard error"
fi

# 4. bare JOIN with no argument
irc_clear alice
irc_send alice "JOIN"
expect_ok alice "461|[Nn]eed more param" 2.0 "bare JOIN with no channel name errors"

# 5. comma-separated multi-channel JOIN
irc_clear alice
irc_send alice "JOIN #multi1,#multi2"
sleep 0.8
if irc_buf alice | grep -qE "#multi1" && irc_buf alice | grep -qE "#multi2"; then
    t_ok "comma-separated JOIN puts the client in BOTH channels"
else
    t_fail "comma-separated JOIN did not confirm both #multi1 and #multi2"
fi

# 6. PART notifies remaining members
irc_clear bob
irc_send alice "PART #jptest"
expect_ok bob "PART.*#jptest|jalice" 2.0 "remaining member sees the PART"

# 7. PART a channel you're not in
irc_clear alice
irc_send alice "PART #jptest"
expect_ok alice "442|[Nn]ot on that channel" 2.0 "PARTing a channel you already left is rejected"

# 8. rejoin after PART
irc_clear alice
irc_send alice "JOIN #jptest"
expect_ok alice "JOIN.*#jptest|#jptest" 2.0 "can rejoin a channel after having PARTed it"

report_summary