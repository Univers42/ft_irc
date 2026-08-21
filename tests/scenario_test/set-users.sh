#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <port> <password>"
    exit 1
fi

PORT="$1"
PASS="$2"

HOST="127.0.0.1"
CHANNEL="#test"

BASE="${TMPDIR:-/tmp}/ft-irc-hexchat"
ALICE_DIR="$BASE/alice"
BOB_DIR="$BASE/bob"

rm -rf "$BASE"
mkdir -p "$ALICE_DIR" "$BOB_DIR"

echo "[+] Testing ft_irc on $HOST:$PORT"
echo "[+] Channel: $CHANNEL"
echo "[+] Alice config: $ALICE_DIR"
echo "[+] Bob config:   $BOB_DIR"

# Start two completely independent HexChat instances.
#
# -d gives each client its own configuration directory.
# -a prevents HexChat from automatically connecting to whatever
#    networks happen to be in the default configuration.
#
# The -c commands are executed by HexChat after it starts.

echo "[+] Starting Alice..."

hexchat \
    -d "$ALICE_DIR" \
    -a \
    -c "set irc_nick1 Alice" \
    -c "set irc_nick2 Alice2" \
    -c "set irc_nick3 Alice3" \
    -c "set irc_user_name alice" \
    -c "set irc_real_name Alice" \
    -c "server $HOST $PORT $PASS" \
    >/tmp/ft-irc-hexchat-alice.log 2>&1 &

ALICE_PID=$!

sleep 2

echo "[+] Starting Bob..."

hexchat \
    -d "$BOB_DIR" \
    -a \
    -c "set irc_nick1 Bob" \
    -c "set irc_nick2 Bob2" \
    -c "set irc_nick3 Bob3" \
    -c "set irc_user_name bob" \
    -c "set irc_real_name Bob" \
    -c "server $HOST $PORT $PASS" \
    >/tmp/ft-irc-hexchat-bob.log 2>&1 &

BOB_PID=$!

echo
echo "=========================================="
echo " Alice PID: $ALICE_PID"
echo " Bob PID:   $BOB_PID"
echo " Server:    $HOST:$PORT"
echo " Channel:   $CHANNEL"
echo "=========================================="
echo
echo "Once both clients connect:"
echo
echo "  Alice: /join $CHANNEL"
echo "  Bob:   /join $CHANNEL"
echo
echo "Then type messages normally."
echo
echo "Logs:"
echo "  /tmp/ft-irc-hexchat-alice.log"
echo "  /tmp/ft-irc-hexchat-bob.log"
echo
echo "To stop both:"
echo "  kill $ALICE_PID $BOB_PID"