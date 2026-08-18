#!/usr/bin/env bash
# The largest single evaluation surface in ft_irc. Assumes first-joiner-is-op.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "08: MODE"
irc_setup
trap irc_teardown EXIT

irc_connect op
irc_connect u1
irc_connect u2
irc_register op mop >/dev/null
irc_register u1 mu1 >/dev/null
irc_register u2 mu2 >/dev/null

irc_send op "JOIN #modetest"
sleep 0.5
irc_send u1 "JOIN #modetest"
sleep 0.5
irc_clear op; irc_clear u1; irc_clear u2

# --- +o -------------------------------------------------------------------
irc_send u1 "MODE #modetest +o mu1"
expect_ok u1 "482|[Nn]ot.*operator" 2.0 "non-operator can't grant +o"

irc_clear op; irc_clear u1
irc_send op "MODE #modetest +o mu1"
expect_ok u1 "\\+o|MODE.*mu1" 2.0 "operator CAN grant +o"

irc_clear u1
irc_send u1 "MODE #modetest +i"
expect_ok u1 "\\+i|MODE" 2.0 "newly promoted operator can immediately use operator commands"
irc_send u1 "MODE #modetest -i"
sleep 0.4

irc_clear op
irc_send op "MODE #modetest -o mu1"
expect_ok op "\\-o|MODE.*mu1" 2.0 "operator status can be revoked with -o"

# --- +i -------------------------------------------------------------------
irc_clear op
irc_send op "MODE #modetest +i"
expect_ok op "\\+i|MODE" 2.0 "+i is settable by an operator"

irc_clear u2
irc_send u2 "JOIN #modetest"
expect_ok u2 "473|[Ii]nvite only" 2.0 "JOIN blocked on invite-only channel"

irc_clear op
irc_send op "MODE #modetest -i"
sleep 0.4
irc_clear u2
irc_send u2 "JOIN #modetest"
expect_ok u2 "JOIN.*#modetest|#modetest" 2.0 "-i lets ordinary JOIN through again"

# --- +k -------------------------------------------------------------------
irc_clear op
irc_send op "MODE #modetest +k secretkey"
expect_ok op "\\+k|MODE" 2.0 "+k <password> is settable by an operator"

irc_send u2 "PART #modetest"
sleep 0.4
irc_clear u2
irc_send u2 "JOIN #modetest"
expect_ok u2 "475|Cannot join|[Bb]ad.*key" 2.0 "JOIN without the key is rejected"

irc_clear u2
irc_send u2 "JOIN #modetest wrongkey"
expect_ok u2 "475|Cannot join|[Bb]ad.*key" 2.0 "JOIN with the WRONG key is rejected"

irc_clear u2
irc_send u2 "JOIN #modetest secretkey"
expect_ok u2 "JOIN.*#modetest|#modetest" 2.0 "JOIN with the correct key succeeds"

irc_clear op
irc_send op "MODE #modetest -k"
expect_ok op "\\-k|MODE" 2.0 "-k removes the channel key"

# --- +l -------------------------------------------------------------------
irc_clear op
irc_send op "MODE #modetest +l 2"
expect_ok op "\\+l|MODE" 2.0 "+l <n> is settable by an operator"

# channel already holds op+u1+u2 == 3, so a fresh client must be refused
irc_connect u3
irc_register u3 mu3 >/dev/null
irc_clear u3
irc_send u3 "JOIN #modetest"
expect_ok u3 "471|[Cc]hannel is full" 2.0 "JOIN beyond the +l limit is rejected"

irc_clear op
irc_send op "MODE #modetest -l"
sleep 0.4
irc_clear u3
irc_send u3 "JOIN #modetest"
expect_ok u3 "JOIN.*#modetest|#modetest" 2.0 "-l removes the limit and JOIN succeeds"

# --- combined flags -------------------------------------------------------
irc_clear op
irc_send op "MODE #modetest +ikl comboKey 5"
sleep 1
if irc_buf op | grep -qiE "^ERROR"; then
    t_fail "combined 'MODE +ikl <key> <limit>' produced a hard ERROR"
else
    t_ok "combined 'MODE +ikl <key> <limit>' is accepted without a hard error"
fi
irc_send op "MODE #modetest -i"
irc_send op "MODE #modetest -k"
irc_send op "MODE #modetest -l"
sleep 0.5

# --- malformed MODE -------------------------------------------------------
irc_clear op
irc_send op "MODE #modetest +k"
expect_ok op "461|[Nn]eed more param" 2.0 "+k with no key argument errors instead of crashing"

irc_clear op
irc_send op "MODE #modetest +l"
expect_ok op "461|[Nn]eed more param" 2.0 "+l with no limit argument errors instead of crashing"

irc_clear op
irc_send op "MODE #modetest +x"
sleep 1
if irc_server_alive; then
    t_ok "unknown mode letter is handled gracefully (server still up)"
else
    t_fail "unknown mode letter took the server down"
fi

report_summary