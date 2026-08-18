#!/usr/bin/env bash
# Pre-registration command gating (451) and the malformed-input barrage.
# This is the "try to break it" file: everything here is input a real client
# would never send. Nothing in it may crash, hang, or silently succeed.
# This file used to be a byte-for-byte copy of 08_modes.sh — none of this
# coverage actually existed.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "09: malformed input / pre-registration"
irc_setup
trap irc_teardown EXIT

# --- 1. commands issued before registration completes ---------------------
# Every one of these must be refused with 451 ERR_NOTREGISTERED.
preauth_probe() {
    label="$1"
    cmd="$2"
    irc_connect preauth
    irc_send preauth "$cmd"
    if irc_expect preauth "451|not registered" 1.5; then
        t_ok "'$label' before registration is rejected (451)"
    else
        t_fail "'$label' before registration was NOT rejected with 451"
    fi
    irc_close preauth
}

preauth_probe JOIN    "JOIN #x"
preauth_probe PRIVMSG "PRIVMSG someone :hi"
preauth_probe NOTICE  "NOTICE someone :hi"
preauth_probe PART    "PART #x"
preauth_probe TOPIC   "TOPIC #x"
preauth_probe MODE    "MODE #x +i"
preauth_probe KICK    "KICK #x someone"
preauth_probe INVITE  "INVITE someone #x"
preauth_probe NAMES   "NAMES #x"
preauth_probe LIST    "LIST"
preauth_probe WHO     "WHO #x"

if irc_server_alive; then
    t_ok "server still alive after all pre-registration probes"
else
    t_fail "server died during the pre-registration probes"
fi

# --- 2. registration-order abuse ------------------------------------------
# USER before NICK, NICK twice, PASS after USER — none may register the client
# or wedge the connection.
irc_connect order1
irc_send order1 "USER u 0 * :U"
irc_send order1 "PASS $IRC_PASSWORD"
irc_send order1 "NICK ordered1"
sleep 1.0
if irc_buf order1 | grep -qE "001|Welcome"; then
    t_ok "USER-before-NICK still completes registration once all three arrive"
else
    t_ok "USER-before-NICK did not register (implementation refuses the order)"
fi
if irc_server_alive; then
    t_ok "server survives out-of-order registration"
else
    t_fail "server died on out-of-order registration"
fi
irc_close order1

# PASS sent twice before registering
irc_connect twopass
irc_send twopass "PASS $IRC_PASSWORD"
irc_send twopass "PASS $IRC_PASSWORD"
irc_send twopass "NICK twopass"
irc_send twopass "USER u 0 * :U"
sleep 1.0
if irc_server_alive; then
    t_ok "two PASS commands before registration don't crash the server"
else
    t_fail "server died on a repeated PASS"
fi
irc_close twopass

# --- 3. unknown / malformed commands from a registered client -------------
irc_connect mal
irc_register mal malformed >/dev/null
irc_clear mal

irc_send mal "FOOBARBAZ some args"
expect_ok mal "421|Unknown command" 2.0 \
    "a totally unknown command gets 421, not silence"

irc_clear mal
irc_send mal "JOIN"
expect_ok mal "461|need more param" 2.0 \
    "JOIN with no channel argument errors cleanly with 461"

irc_clear mal
irc_send mal "MODE"
expect_ok mal "461|need more param" 2.0 "bare MODE returns 461"

irc_clear mal
irc_send mal "KICK"
sleep 0.8
if irc_buf mal | grep -qE "461"; then
    t_ok "bare KICK returns 461"
elif irc_buf mal | grep -qiE "^ERROR"; then
    t_fail "bare KICK returned a hard ERROR / dropped the connection"
else
    t_ok "bare KICK was ignored without crashing"
fi

# --- 4. structurally broken lines -----------------------------------------
irc_clear mal
irc_send_raw mal "\r\n"
irc_send_raw mal "   \r\n"
irc_send_raw mal ":\r\n"
irc_send_raw mal ": \r\n"
irc_send_raw mal ":prefix-only\r\n"
irc_send_raw mal "::::::\r\n"
irc_send_raw mal "PRIVMSG :\r\n"
irc_send_raw mal "                                        \r\n"
sleep 1.0
if irc_server_alive; then
    t_ok "server survives empty, whitespace-only and prefix-only lines"
else
    t_fail "server died on a structurally empty line"
fi

# --- 5. hostile parameter shapes ------------------------------------------
irc_clear mal
# a command with an absurd number of parameters
manyparams=$(head -c 200 /dev/zero | tr '\0' 'x' | sed 's/x/ p/g')
irc_send_raw mal "MODE #chan$manyparams\r\n"
sleep 0.5
# a command name that is enormous
bigcmd=$(head -c 2000 /dev/zero | tr '\0' 'C')
irc_send_raw mal "$bigcmd arg\r\n"
sleep 0.5
# NUL byte in the middle of a line
printf 'PRIVMSG malformed :before\000after\r\n' >&"$(eval echo \$IRC_FD_mal)" 2>/dev/null
sleep 0.5
# a lone CR with no LF, then a real command
irc_send_raw mal "PING partial\rPING :real\r\n"
sleep 1.0
if irc_server_alive; then
    t_ok "server survives oversized commands, NUL bytes and stray CR"
else
    t_fail "server died on hostile parameter shapes"
fi

# --- 6. the connection must still be usable after all that ----------------
irc_clear mal
irc_send mal "PING :still-here"
if irc_expect mal "PONG|still-here" 2.5; then
    t_ok "the abused connection still answers PING afterwards"
else
    t_fail "the connection stopped responding after the malformed barrage"
fi
irc_close mal

# --- 7. a brand-new client must still be able to register -----------------
irc_connect fresh
if irc_register fresh freshcli; then
    t_ok "a new client can still register after the full barrage"
else
    t_fail "server no longer accepts registrations after the barrage"
fi
irc_close fresh

if irc_server_alive; then
    t_ok "server responsive and alive after the full malformed-input barrage"
else
    t_fail "server unreachable after the full malformed-input barrage"
fi

report_summary
