```py
#!/usr/bin/env bash
# Section 4.4 — the ^Z backpressure test, exactly as the sheet asks:
#   freeze a joined client (SIGSTOP == ^Z), flood the channel, and check that
#   the server neither hangs nor spins, that others stay responsive, and that
#   the frozen client resumes cleanly.
PORT=${1:-6767}
S=/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad
SRVPID=$(pgrep -f 'ircserv.snapshot' | head -1)
echo "server pid = $SRVPID"

cpu() { ps -o %cpu= -p "$SRVPID" | tr -d ' '; }
jiffies() { awk '{print $14+$15}' /proc/"$SRVPID"/stat; }

# --- the client that will be frozen -----------------------------------------
FIFO=$S/slowin.$$
mkfifo "$FIFO"
nc -C 127.0.0.1 "$PORT" < "$FIFO" > "$S/slow.out" 2>&1 &
NCPID=$!
exec 9>"$FIFO"
printf 'PASS pass\r\nNICK slow\r\nUSER s 0 * :S\r\nJOIN #flood\r\n' >&9
sleep 1
echo "slow client registered: $(grep -c ' 001 ' "$S/slow.out") welcome line(s)"

J0=$(jiffies)
echo "--- freezing the client (SIGSTOP == ^Z) ---"
kill -STOP "$NCPID"

# --- flood the channel ------------------------------------------------------
echo "--- flooding 5000 lines into #flood ---"
{
  printf 'PASS pass\r\nNICK loud\r\nUSER l 0 * :L\r\nJOIN #flood\r\n'
  for i in $(seq 1 5000); do printf 'PRIVMSG #flood :flood %d\r\n' "$i"; done
  sleep 3
} | nc -C -q3 127.0.0.1 "$PORT" > "$S/loud.out" 2>&1

sleep 1
J1=$(jiffies)
echo "server CPU jiffies used during the flood: $((J1-J0)) (100 = 1 full second of CPU)"
echo "instantaneous %CPU: $(cpu)"

echo "--- Send-Q on the frozen client's socket ---"
ss -tn "( sport = :$PORT )" 2>/dev/null | awk 'NR==1 || $3>0' | head -5

echo "--- is a third client still served while one is frozen? ---"
printf 'PASS pass\r\nNICK witness\r\nUSER w 0 * :W\r\nJOIN #flood\r\nPRIVMSG #flood :responsive\r\n' \
  | timeout 8 nc -C -q2 127.0.0.1 "$PORT" > "$S/witness.out" 2>&1
grep -qE ' 001 ' "$S/witness.out" && echo "  ok   witness registered while the other client was frozen" \
                                  || echo "  ***  witness did NOT register"

echo "--- resuming the frozen client (fg) ---"
kill -CONT "$NCPID"
sleep 2
echo "lines the resumed client ended up with: $(wc -l < "$S/slow.out")"
grep -c 'flood ' "$S/slow.out" | sed 's/^/  flood messages delivered: /'
if grep -qi 'sendq\|ERROR' "$S/slow.out"; then
  echo "  note: the client was dropped for exceeding SendQ (documented behaviour):"
  grep -i 'sendq\|ERROR' "$S/slow.out" | tail -2
fi

exec 9>&-
kill -9 "$NCPID" 2>/dev/null
rm -f "$FIFO"

echo "--- server still alive and serving? ---"
printf 'PASS pass\r\nNICK afterfl\r\nUSER a 0 * :A\r\nPING :x\r\n' \
  | timeout 6 nc -C -q1 127.0.0.1 "$PORT" | grep -qE 'PONG' \
  && echo "  ok   server answers after the flood" || echo "  ***  server not answering"


```


server pid = 1182071
slow client registered: 1 welcome line(s)
--- freezing the client (SIGSTOP == ^Z) ---
--- flooding 5000 lines into #flood ---
server CPU jiffies used during the flood: 7 (100 = 1 full second of CPU)
instantaneous %CPU: 0.0
--- Send-Q on the frozen client's socket ---
State Recv-Q Send-Q Local Address:Port Peer Address:Port Process
ESTAB 0      151885     127.0.0.1:6767    127.0.0.1:48998
--- is a third client still served while one is frozen? ---
  ok   witness registered while the other client was frozen
--- resuming the frozen client (fg) ---
lines the resumed client ended up with: 5017
  flood messages delivered: 5006
t44_flood.sh: line 62: 1184190 Killed                  nc -C 127.0.0.1 "$PORT" < "$FIFO" > "$S/slow.out" 2>&1
--- server still alive and serving? ---
  ok   server answers after the flood
exit=0
--- exits ---

  timeout 300 python3 -c "
  import sys; sys.path.insert(0,'.')
  import ircprobe as P
  PORT=6767
  def cl(t): return ' | '.join(l for l in t.split('\r\n') if l) or '(nothing)'
  print('== 3.5 channel broadcast, five-row table ==')
  a=P.register(PORT,'alice35'); a.send('JOIN #b35'); a.collect(); a.buf=b''
  b=P.register(PORT,'bob35')
  b.send('JOIN #b35'); tb=b.collect(); ta=a.collect()
  print('1 bob JOINs   | alice sees:', cl(ta)[:70])
  print('              | bob sees  :', cl(tb).split('422 ')[-1][:110])
  a.buf=b''; b.buf=b''
  a.send('PRIVMSG #b35 :hello'); ta=a.collect(); tb=b.collect()
  print('2 alice PRIVMSG | alice sees:', cl(ta), '| bob sees:', cl(tb))
  a.buf=b''; b.buf=b''
  b.send('PRIVMSG #b35 :hi back'); tb=b.collect(); ta=a.collect()
  print('3 bob PRIVMSG   | alice sees:', cl(ta), '| bob sees:', cl(tb))
  a.buf=b''; b.buf=b''
  b.send('PART #b35'); tb=b.collect(); ta=a.collect()
  print('4 bob PARTs     | alice sees:', cl(ta), '| bob sees:', cl(tb))
  a.buf=b''; b.buf=b''
  b.send('JOIN #b35'); b.collect(); a.collect(); a.buf=b''; b.buf=b''
  b.send('QUIT :bye'); tb=b.collect(); ta=a.collect()
  print('5 bob QUITs     | alice sees:', cl(ta), '| bob sees:', cl(tb) or '(closed)')
  a.close(); b.close()
  print()
  print('== 3.4 ten simultaneous clients ==')
  import threading
  ok=[]
  def one(i):
      t=P.reg_then(PORT,'bot%d'%i,'JOIN #eval34','PRIVMSG #eval34 :hello from bot%d'%i)
      ok.append('JOIN' in t)
  ths=[threading.Thread(target=one,args=(i,)) for i in range(10)]
  [t.start() for t in ths]; [t.join() for t in ths]
  print('  10 parallel register+join+privmsg all succeeded:', all(ok), '(%d/10)'%sum(ok))
  " 2>&1)
== 3.5 channel broadcast, five-row table ==
1 bob JOINs   | alice sees: :bob35!bob35@127.0.0.1 JOIN #b35
              | bob sees  : bob35 :MOTD File is missing | :bob35!bob35@127.0.0.1 JOIN #b35 | :ft_irc 331 bob35 #b35 :No topic is set | :ft
2 alice PRIVMSG | alice sees: (nothing) | bob sees: :alice35!alice35@127.0.0.1 PRIVMSG #b35 :hello
3 bob PRIVMSG   | alice sees: :bob35!bob35@127.0.0.1 PRIVMSG #b35 :hi back | bob sees: (nothing)
4 bob PARTs     | alice sees: :bob35!bob35@127.0.0.1 PART #b35 | bob sees: :bob35!bob35@127.0.0.1 PART #b35
5 bob QUITs     | alice sees: :bob35!bob35@127.0.0.1 QUIT :bye | bob sees: (nothing)

== 3.4 ten simultaneous clients ==
  10 parallel register+join+privmsg all succeeded: True (10/10)
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 5m)
