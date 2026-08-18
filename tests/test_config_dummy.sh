#!/usr/bin/env bash

: "${PROJECT_DIR:=DUMMY_PROJECT_DIR}"
: "${BIN:=DUMMY_BIN}"
: "${IRC_HOST:=DUMMY_HOST}"
: "${IRC_PORT:=DUMMY_PORT}"
: "${IRC_PASSWORD:=DUMMY_PASSWORD}"
: "${STARTUP_PORT:=DUMMY_STARTUP_PORT}"

export PROJECT_DIR BIN IRC_HOST IRC_PORT IRC_PASSWORD STARTUP_PORT

echo "Dummy configuration applied where variables were empty/unset."

printf 'PROJECT_DIR   = %s\n' "$PROJECT_DIR"
printf 'BIN           = %s\n' "$BIN"
printf 'IRC_HOST      = %s\n' "$IRC_HOST"
printf 'IRC_PORT      = %s\n' "$IRC_PORT"
printf 'IRC_PASSWORD  = %s\n' "$IRC_PASSWORD"
printf 'STARTUP_PORT  = %s\n' "$STARTUP_PORT"