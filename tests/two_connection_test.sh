#!/usr/bin/env bash

PORT="${PORT:-6667}"
PASS="${PASS:-pass}"

test_connection() {
    local protocol="$1"
    local host="$2"
    local nick="$3"

    echo "== Testing $protocol: $host =="

    {
        printf 'PASS %s\r\n' "$PASS"
        printf 'NICK %s\r\n' "$nick"
        printf 'USER %s 0 * :%s Test\r\n' "$nick" "$protocol"
        sleep 1
    } | timeout 3 nc "$4" "$host" "$PORT" |
        grep -q ' 001 '

    if [ $? -eq 0 ]; then
        echo "$protocol: PASS"
    else
        echo "$protocol: FAIL"
    fi

    echo
}

test_connection "IPv4" "127.0.0.1" "ipv4test" "-4"
test_connection "IPv6" "::1"       "ipv6test" "-6"