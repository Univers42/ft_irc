#!/bin/bash

SERVER="127.0.0.1"
PORT="6000"
PASS="your_password"
NICK="testbot"
USER="testbot"

CHANNELS=(
    "#test-right-alice"
    "#test-right-bob"
    "#test-null-alice"
    "#test-null-bob"
    "#test-join-valid"
    "#test-join-invalid"
    "#test-join-null"
    "#test-kick-valid"
    "#test-kick-edge"
)

{
    echo "PASS $PASS"
    echo "NICK $NICK"
    echo "USER $USER 0 * :Test Bot"

    for channel in "${CHANNELS[@]}"; do
        echo "JOIN $channel"
    done

} | nc "$SERVER" "$PORT"
