``` py
#!/usr/bin/env bash
# 10.1 full-tier valgrind with a clean shutdown, and 10.2 leaks during the flood.
S=/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad
cd /home/dlesieur/Documents/ft_irc || exit 1
PORT=${1:-6868}
MODE=${2:-basic}
LOG=$S/vg-$MODE.log

valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
         --error-exitcode=42 ./build/bin/ircserv "$PORT" pass > "$LOG" 2>&1 &
VG=$!
for i in $(seq 1 60); do ss -ltn | grep -q ":$PORT " && break; sleep 0.5; done
ss -ltn | grep -q ":$PORT " || { echo "server never listened"; cat "$LOG"; exit 1; }
echo "listening on $PORT (valgrind pid $VG)"

if [ "$MODE" = basic ]; then
    for i in $(seq 1 20); do
        printf 'PASS pass\r\nNICK v%d\r\nUSER v 0 * :V\r\nJOIN #vg\r\nPRIVMSG #vg :m%d\r\nQUIT :bye\r\n' "$i" "$i" \
            | timeout 10 nc -C -q1 127.0.0.1 "$PORT" >/dev/null 2>&1
    done
    # leave two clients connected at shutdown, and one mid-command
    exec 8<>/dev/tcp/127.0.0.1/$PORT
    printf 'PASS pass\r\nNICK alive1\r\nUSER a 0 * :A\r\nJOIN #vg\r\n' >&8
    exec 7<>/dev/tcp/127.0.0.1/$PORT
    printf 'PASS pass\r\nNICK half1\r\nUSER h 0 * :H\r\nPRIV' >&7
    sleep 1
else
    # 10.2 — the ^Z flood, under valgrind
    FIFO=$S/vgslow.$$; mkfifo "$FIFO"
    nc -C 127.0.0.1 "$PORT" < "$FIFO" > "$S/vgslow.out" 2>&1 &
    NCPID=$!
    exec 9>"$FIFO"
    printf 'PASS pass\r\nNICK slow\r\nUSER s 0 * :S\r\nJOIN #f\r\n' >&9
    sleep 2
    kill -STOP "$NCPID"
    { printf 'PASS pass\r\nNICK loud\r\nUSER l 0 * :L\r\nJOIN #f\r\n'
      for i in $(seq 1 3000); do printf 'PRIVMSG #f :flood %d\r\n' "$i"; done
      sleep 2; } | timeout 120 nc -C -q2 127.0.0.1 "$PORT" >/dev/null 2>&1
    kill -CONT "$NCPID"; sleep 2
    exec 9>&-; kill -9 "$NCPID" 2>/dev/null; rm -f "$FIFO"
fi

kill -INT "$VG"
wait "$VG"; RC=$?
echo "valgrind exit=$RC"
grep -E "in use at exit|total heap usage|definitely lost|indirectly lost|possibly lost|still reachable|All heap blocks|ERROR SUMMARY" "$LOG"



```