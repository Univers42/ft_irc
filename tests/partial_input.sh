#!/usr/bin/env bash

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6667}"


{
    printf 'PASS pass\r\n'
    printf 'NI'
    sleep 2
    printf 'CK partial\r\n'
    printf 'USER partial 0 * :Partial Test\r\n'
    sleep 1
} | timeout 5 nc "$HOST" "$PORT" | cat -v

echo
echo "Expected: registration succeeds only after the NICK arrives."
