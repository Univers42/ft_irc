 Bash(cd /tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad
  timeout 300 python3 -c "
  import sys, os; sys.path.insert(0,'.')
  import ircprobe as P
  PORT=6767
  def verbs(t): return ','.join(l.split()[1] for l in t.split('\r\n') if len(l.split())>1) or '-'
  def echo(t): return ' | '.join(l for l in t.split('\r\n') if ' MODE ' in l) or '(no echo)'

  # boundary: how many +o params does one MODE accept?
  for count in (11,12,13,14,15):
      op=P.register(PORT,'bd%d'%count); ch='#bd%d'%count
      op.send('JOIN '+ch); op.collect()
      ms=[]
      for i in range(count):
          n='q%d_%d'%(count,i); m=P.register(PORT,n); m.send('JOIN '+ch); m.collect(); ms.append((n,m))
      op.collect(); op.buf=b''
      op.send('MODE %s +%s %s'%(ch,'o'*count,' '.join(n for n,_ in ms)))
      t=op.collect(quiet=0.8)
      applied = echo(t).count(' q%d_'%count)
      print('  %2d params -> nums=%-12s nicks echoed=%d' % (count, ','.join(P.numerics(t)) or '-', applied))
      for _,m in ms: m.close()
      op.close()

  print()
  # a very long modestring with no params: is the echo split or coalesced?
  op=P.register(PORT,'lng'); op.send('JOIN #lng'); op.collect(); op.buf=b''
  op.send('MODE #lng +'+('it'*200))
  t=op.collect(quiet=0.8)
  print('  MODE #lng + (it x200): nums=%s' % (','.join(P.numerics(t)) or '-'))
  print('  echo lines=%d longest=%d' % (len([l for l in t.split(chr(13)+chr(10)) if ' MODE ' in l]),
        max([len(l)+2 for l in t.split(chr(13)+chr(10)) if ' MODE ' in l] or [0])))
  print('  echo:', echo(t)[:120])
  op.close()
  " 2>&1; cat exits.log)