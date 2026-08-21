#!/usr/bin/env bash
# The area most ft_irc implementations quietly get wrong: assuming one
# recv() == one command. Over real TCP it never is.
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "03: TCP framing"
irc_setup
trap irc_teardown EXIT

# 1. command split across many writes, one character at a time
irc_connect frag
irc_send_fragmented frag "PASS $IRC_PASSWORD" 0.01
irc_send_fragmented frag "NICK fragger" 0.01
irc_send_fragmented frag "USER u 0 * :U" 0.01
expect_ok frag "001|Welcome" 3.0 \
    "PASS/NICK/USER sent one byte at a time still registers correctly"
irc_close frag

# 2. several complete commands in a single write
irc_connect batch
irc_send_raw batch "PASS $IRC_PASSWORD\r\nNICK batcher\r\nUSER u 0 * :U\r\n"
expect_ok batch "001|Welcome" 2.0 \
    "PASS+NICK+USER sent as one single packet all get parsed"
irc_close batch

# 3. complete commands plus the START of another, unterminated
irc_connect spill
irc_send_raw spill "PASS $IRC_PASSWORD\r\nNICK spiller\r\nUSER u 0 * :U\r\nPRIV"
sleep 0.5
irc_send_raw spill "MSG spiller :hi\r\n"
expect_ok spill "001|Welcome" 2.0 \
    "registers correctly despite a trailing partial command in the same packet"
irc_clear spill
irc_send spill "PING still-alive"
sleep 0.5
if irc_server_alive; then
    t_ok "server still responsive after the spliced/partial command"
else
    t_fail "server unresponsive after the spliced/partial command"
fi
irc_close spill

# 4. bare LF instead of CRLF
irc_connect lf
irc_send_raw lf "PASS $IRC_PASSWORD\nNICK lfonly\nUSER u 0 * :U\n"
expect_ok lf "001|Welcome" 2.0 \
    "commands terminated with bare \\n (no \\r) are still accepted"
irc_close lf

# 5. stray empty lines between commands
irc_connect blank
irc_send_raw blank "\r\n\r\nPASS $IRC_PASSWORD\r\n\r\nNICK blanker\r\nUSER u 0 * :U\r\n\r\n"
expect_ok blank "001|Welcome" 2.0 \
    "stray empty lines between commands don't break parsing"
irc_close blank

# 6. one absurdly long line
irc_connect huge
irc_register huge hugecli >/dev/null
irc_clear huge
bigpayload=$(head -c 20000 /dev/zero | tr '\0' 'X')
irc_send_raw huge "PRIVMSG hugecli :$bigpayload\r\n"
sleep 1
irc_connect probe1
if irc_register probe1 probehuge; then
    t_ok "server still accepts new registrations after a 20KB single line"
else
    t_fail "server broken after receiving a 20KB single line"
fi
irc_close probe1
irc_close huge

# 7. raw garbage bytes, then a valid command on the same connection
irc_connect garbage
irc_send_raw garbage "\001\002\177 not-a-command \007\r\n"
irc_send garbage "PASS $IRC_PASSWORD"
irc_send garbage "NICK garbler"
irc_send garbage "USER u 0 * :U"
expect_ok garbage "001|Welcome" 2.0 \
    "garbage bytes before a valid command don't wedge the connection"
irc_close garbage

report_summary