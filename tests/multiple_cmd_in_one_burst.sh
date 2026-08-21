#!/usr/bin/env bash

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6667}"

echo "== Multiple commands test =="

printf \
'PASS pass\r\nNICK burst\r\nUSER burst 0 * :Burst Test\r\nPING hello\r\nPING world\r\n' |
timeout 3 nc "$HOST" "$PORT" | cat -v

echo
echo "Expected: registration and both PING commands are processed."