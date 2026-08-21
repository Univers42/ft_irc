#!/usr/bin/env bash
# 1. server doesn't fall over with many clients doing normal things
# 2. one client that never reads its socket can't stall everyone else — the
#    giveaway for a blocking send() hiding in the write path
# Portable POSIX shell: this file must behave identically under bash and
# under hellish, so no BASH_SOURCE, no arrays, no [[ ]].
cd "$(dirname "$0")" || exit 1
. ./config.sh
. ./lib/irc_lib.sh

report_init "10: multi-client stress"
irc_setup
trap irc_teardown EXIT

# --- 1. fan-in: N clients register and join the same channel --------------
N=${STRESS_CLIENTS:-12}
failed=0
i=1
while [ "$i" -le "$N" ]; do
    irc_connect "s$i"
    if ! irc_register "s$i" "stress$i"; then
        failed=$((failed + 1))
    fi
    i=$((i + 1))
done

if [ "$failed" -eq 0 ]; then
    t_ok "all $N clients registered"
else
    t_fail "$failed of $N clients failed to register"
fi

i=1
while [ "$i" -le "$N" ]; do
    irc_send "s$i" "JOIN #stress"
    i=$((i + 1))
done
sleep 1.5

if irc_server_alive; then
    t_ok "server still alive after $N clients joined the same channel"
else
    t_fail "server died with $N clients on one channel"
fi

i=1
while [ "$i" -le "$N" ]; do
    irc_close "s$i"
    i=$((i + 1))
done

# --- 2. non-reading client must not block the rest ------------------------
irc_connect slowpoke
irc_register slowpoke slowpoke >/dev/null
irc_send slowpoke "JOIN #slowtest"
sleep 0.5

irc_connect flooder
irc_register flooder flooder >/dev/null
irc_send flooder "JOIN #slowtest"
sleep 0.5

irc_connect probe2
irc_register probe2 probecl >/dev/null   # <=9 chars: server NICKLEN=9
irc_clear probe2

# SIGSTOP the slowpoke's nc: it stops draining the socket, its receive
# buffer fills and stays full for the rest of this block.
irc_freeze slowpoke

bigmsg=$(head -c 800 /dev/zero | tr '\0' 'X')
start=$(date +%s)
i=1
while [ "$i" -le 300 ]; do
    irc_send_raw flooder "PRIVMSG #slowtest :$bigmsg\r\n"
    i=$((i + 1))
done
irc_send_raw flooder "PRIVMSG probecl :flood-done-marker\r\n"

if irc_expect probe2 "flood-done-marker" 10.0; then
    t_ok "an unrelated client still received its direct message during the flood"
else
    t_fail "unrelated client never got its message — server stalled on the frozen client"
fi
end=$(date +%s)
elapsed=$((end - start))

if [ "$elapsed" -lt 10 ]; then
    t_ok "flood resolved in ${elapsed}s (<10s budget) — not stalled by the non-reading client"
else
    t_fail "flood took ${elapsed}s — server appears blocked on the non-reading client"
fi

if irc_server_alive; then
    t_ok "server still accepting new connections while a client's buffer is backed up"
else
    t_fail "server stopped accepting connections during the flood"
fi

irc_thaw slowpoke
irc_close slowpoke
irc_close flooder
irc_close probe2

# --- 3. abrupt disconnect mid-activity, survivors keep working ------------
irc_connect sa
irc_connect sb
irc_connect sv
irc_register sa surva >/dev/null
irc_register sb survb >/dev/null
irc_register sv survv >/dev/null
irc_send sa "JOIN #survive"
irc_send sb "JOIN #survive"
irc_send sv "JOIN #survive"
sleep 0.8

irc_kill_hard sv
irc_clear sa
irc_clear sb
irc_send sa "PRIVMSG #survive :still here?"
expect_ok sb "still here" 3.0 \
    "remaining members still exchange messages right after a peer's abrupt disconnect"

report_summary