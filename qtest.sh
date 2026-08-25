#!/usr/bin/env bash

HOST="10.11.7.5"
PORT="6667"

PASS="pass"
NICK="hellohellohello"
USER="dylanjsfkkahfkajfhkjahfjhsdfkhskhfkslhfjhsdfjhalhsfhlskdhfhlks"
MODE="1"
UNUSED="         *          "
REALNAME="A"

TEST="REGISTER"

case "$TEST" in
    PASS)
        printf 'PASS %s\r\n' "$PASS"
        ;;

    NICK)
        printf 'NICK %s\r\n' "$NICK"
        ;;

    USER)
        printf 'USER %s %s %s :%s\r\n' \
            "$USER" "$MODE" "$UNUSED" "$REALNAME"
        ;;

    REGISTER)
        printf 'PASS %s\r\nNICK %s\r\nUSER %s %s %s :%s\r\n' \
            "$PASS" "$NICK" "$USER" "$MODE" "$UNUSED" "$REALNAME"
        ;;

    *)
        echo "Unknown TEST=$TEST" >&2
        exit 1
        ;;
esac | timeout 1 nc "$HOST" "$PORT"
