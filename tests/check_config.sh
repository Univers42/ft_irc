#!/usr/bin/env bash
set -u

CONFIG="$(dirname "${BASH_SOURCE[0]}")/config.sh"

if [[ "$(realpath "${BASH_SOURCE[0]}" 2>/dev/null)" == "$(realpath "$CONFIG" 2>/dev/null)" ]]; then
    echo "ERROR: this script and CONFIG resolve to the same file ($CONFIG) — would self-source infinitely" >&2
    exit 1
fi

echo "Loading config: $CONFIG"
echo

if [[ ! -f "$CONFIG" ]]; then
    echo "ERROR: config file not found"
    exit 1
fi

source "$CONFIG"

echo "=== Values ==="
printf "PROJECT_DIR   = %s\n" "$PROJECT_DIR"
printf "BIN           = %s\n" "$BIN"
printf "IRC_HOST      = %s\n" "$IRC_HOST"
printf "IRC_PORT      = %s\n" "$IRC_PORT"
printf "IRC_PASSWORD  = %s\n" "$IRC_PASSWORD"
printf "STARTUP_PORT  = %s\n" "$STARTUP_PORT"
