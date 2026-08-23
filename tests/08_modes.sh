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

# --- the mode string must open with a sign --------------------------------
# wiki/FT_IRC_CLIENT_PROTOCOL/signatures.md: "+i", "-o", "-o+i-t" are mode
# strings; "i", "it", "o mu1" are not. There is no implicit "+", so an
# unsigned string applies nothing and answers nothing at all.
irc_clear op
irc_send op "MODE #modetest i"
expect_none op "MODE #modetest" 1.5 "unsigned 'i' is not applied"

irc_clear op
irc_send op "MODE #modetest it"
expect_none op "MODE #modetest" 1.5 "unsigned 'it' is not applied"

irc_clear op
irc_send op "MODE #modetest o mu1"
expect_none op "MODE #modetest" 1.5 "unsigned 'o <nick>' is not applied"

irc_clear op
irc_send op "MODE #modetest xyz"
expect_none op "472" 1.5 "unsigned junk is not answered 472 either"

# The channel must be untouched by all of the above.
irc_clear op
irc_send op "MODE #modetest"
expect_ok op "324 mop #modetest \+" 2.0 "no unsigned MODE leaked a flag onto the channel"

# --- cumulative mode strings ----------------------------------------------
irc_clear op
irc_send op "MODE #modetest +ii"
expect_ok op "MODE #modetest \+ii" 2.0 "'+ii' applies twice and echoes both"

irc_clear op
irc_send op "MODE #modetest -ii"
expect_ok op "MODE #modetest -ii" 2.0 "'-ii' is the reverse of the same shape"

irc_clear op
irc_send op "MODE #modetest -oo mu1 mu2"
expect_ok op "MODE #modetest -oo mu1 mu2" 2.0 "'-oo' draws one parameter per o"

irc_clear op
irc_send op "MODE #modetest -oi mu1"
expect_ok op "MODE #modetest -oi mu1" 2.0 "'-oi' mixes a parameterised and a flag mode"

# --- the sign may flip mid-string -----------------------------------------
irc_clear op
irc_send op "MODE #modetest -o+i-t mu1"
expect_ok op "MODE #modetest -o\+i-t mu1" 2.0 "'-o+i-t' applies each letter under the sign in force"

irc_clear op
irc_send op "MODE #modetest -i+t"
expect_ok op "MODE #modetest -i\+t" 2.0 "a mid-string flip is echoed back as written"

irc_clear op
irc_send op "MODE #modetest +t+t"
expect_ok op "MODE #modetest \+tt" 2.0 "a redundant sign is not restated in the echo"

irc_clear op
irc_send op "MODE #modetest +-i"
expect_ok op "MODE #modetest -i" 2.0 "the last sign before a letter is the one that applies"

irc_clear op
irc_send op "MODE #modetest +"
expect_none op "MODE #modetest" 1.5 "a lone sign applies nothing and stays silent"

# --- '-k' must not eat the parameter a following mode needs ---------------
irc_clear op
irc_send op "MODE #modetest +k eatenkey"
sleep 0.5
irc_clear op
irc_send op "MODE #modetest -k+o mu1"
expect_ok op "MODE #modetest -k\+o mu1" 2.0 "'-k+o <nick>' leaves the parameter to +o"
expect_none op "461" 1.0 "...and does not starve +o into a 461"

irc_clear op
irc_send op "MODE #modetest -o mu1"
sleep 0.5

# --- a dense mode string stays inside the 512-byte line limit -------------
# 481 mode characters: every one applies, and the echo carries a prefix the
# request did not, so on one line it would overrun 512 and be cut. It has to
# be split into whole lines instead.
irc_clear op
_dense="+"
_n=0
while [ "$_n" -lt 240 ]; do
    _dense="${_dense}it"
    _n=$((_n + 1))
done
irc_send op "MODE #modetest $_dense"
sleep 1
# irc_buf splits on LF, so each line still carries its CR but has lost the LF:
# an on-the-wire line of the maximum legal 512 bytes measures 511 here.
_worst=$(irc_buf op | awk '{ if (length($0) > m) m = length($0) } END { print m + 0 }')
_worst=$((_worst + 1))
if [ "$_worst" -le 512 ]; then
    t_ok "a 481-character mode string echoes in lines within 512 (longest: $_worst)"
else
    t_fail "a dense MODE echo overran the line limit ($_worst bytes)"
fi

# Split, not truncated: every one of the 480 letters has to survive somewhere
# in the echo, or the server quietly dropped changes it had already applied.
# A small overcount is fine and unavoidable — "#modetest" contributes an i
# and two t's per echo line. The property under test is that none of the 480
# go missing, so a floor is the right shape of assertion.
_letters=$(irc_buf op | tr -cd 'it' | wc -c | tr -d ' ')
if [ "$_letters" -ge 480 ]; then
    t_ok "all 480 mode letters survive the split ($_letters seen)"
else
    t_fail "the dense echo lost letters — truncated, not split ($_letters of 480)"
fi

irc_send op "MODE #modetest -it"
sleep 0.5

report_summary
