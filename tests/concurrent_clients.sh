#!/usr/bin/env bash

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6667}"
PASS="${PASS:-pass}"

TAG=$(( $(date +%s) % 10000 ))

echo "== Concurrent clients test =="

for i in $(seq 1 10); do
    (
        NICK="t${TAG}${i}"
        LOG="/tmp/irc_concurrent_${i}.log"

        {
            printf 'PASS %s\r\n' "$PASS"
            printf 'NICK %s\r\n' "$NICK"
            printf 'USER %s 0 * :Test %d\r\n' "$NICK" "$i"
        } | timeout 3 nc "$HOST" "$PORT" > "$LOG"

        if grep -q ' 001 ' "$LOG"; then
            echo "client $i ($NICK): PASS"
        else
            echo "client $i ($NICK): FAIL"
            echo "  response:"
            cat -v "$LOG"
        fi
    ) &
done

wait
echo "Done."