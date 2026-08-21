HOST="${HOST:-127.0.0.1}"
PORT="${1:-6667}"
PASS="${2:-pass}"

echo "== NO-FORK_TEST=="
echo "Connect clients while this runs."
echo "If we see fork(), vfork(), or clone(), investigate."

exec strace -f -e trace=process ./ircserv "$PORT" "$PASS"