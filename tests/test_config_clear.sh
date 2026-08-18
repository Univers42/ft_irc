#!/usr/bin/env bash

unset PROJECT_DIR
unset BIN
unset IRC_HOST
unset IRC_PORT
unset IRC_PASSWORD
unset STARTUP_PORT

echo "Configuration variables cleared."

for var in PROJECT_DIR BIN IRC_HOST IRC_PORT IRC_PASSWORD STARTUP_PORT; do
    if [[ -v "$var" ]]; then
        echo "ERROR: $var is still set"
    else
        echo "OK: $var is unset"
    fi
done