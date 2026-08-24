  python3 -c "
  import ircprobe as P
  def verbs(t):
      return ','.join(l.split()[1] for l in t.split('\r\n') if len(l.split())>1) or '(silence)'

  print('== INVITE as regular member on a +i channel (bob joins BEFORE +i) ==')
  a=P.register(6667,'ia'); a.send('JOIN #inv'); a.collect()
  b=P.register(6667,'ib'); b.send('JOIN #inv'); b.collect()
  c=P.register(6667,'ic')
  a.buf=b''; a.send('MODE #inv +i'); a.collect(); b.buf=b''
  b.send('INVITE ic #inv'); print('  regular, +i  ->', verbs(b.collect()))
  a.buf=b''; a.send('INVITE ic #inv'); print('  operator,+i  ->', verbs(a.collect()))
  # and on a -i channel as a regular member
  a2=P.register(6667,'ja'); a2.send('JOIN #inv2'); a2.collect()
  b2=P.register(6667,'jb'); b2.send('JOIN #inv2'); b2.collect(); b2.buf=b''
  b2.send('INVITE ic #inv2'); print('  regular, -i  ->', verbs(b2.collect()))
  for s in (a,b,c,a2,b2): s.close()

  print()
  print('== JOIN / PART comma lists (RFC multi-target) ==')
  d=P.register(6667,'ml')
  d.send('JOIN #l1,#l2,#l3'); print('  JOIN #l1,#l2,#l3 ->', verbs(d.collect()))
  d.buf=b''
  d.send('PART #l1,#l2'); print('  PART #l1,#l2     ->', verbs(d.collect()))
  d.buf=b''
  d.send('JOIN #k1,#k2 key1,key2'); print('  JOIN with keylist ->', verbs(d.collect()))
  d.buf=b''
  d.send('JOIN 0'); print('  JOIN 0 (part all) ->', verbs(d.collect()))
  d.close()

  print()
  print('== KICK multi-target, explicitly ==')
  e=P.register(6667,'ka'); f=P.register(6667,'kb'); g=P.register(6667,'kc')
  e.send('JOIN #x1','JOIN #x2'); e.collect()
  f.send('JOIN #x1','JOIN #x2'); f.collect()
  g.send('JOIN #x1'); g.collect(); e.collect(); e.buf=b''
  e.send('KICK #x1,#x2 kb :multi'); print('  KICK #x1,#x2 kb  ->', verbs(e.collect()))
  e.buf=b''
  e.send('KICK #x1 kb,kc :multi'); print('  KICK #x1 kb,kc   ->', verbs(e.collect()))
  e.buf=b''
  e.send('KICK #x1 kb :single'); print('  KICK #x1 kb      ->', verbs(e.collect()))
  for s in (e,f,g): s.close()
  ")
== INVITE as regular member on a +i channel (bob joins BEFORE +i) ==
  regular, +i  -> MODE,482
  operator,+i  -> 341
  regular, -i  -> 341

== JOIN / PART comma lists (RFC multi-target) ==
  JOIN #l1,#l2,#l3 -> 001,002,003,004,005,422,JOIN,331,353,366,324,329,JOIN,331,353,366,324,329,JOIN,331,353,366,324,329
  PART #l1,#l2     -> PART,PART
  JOIN with keylist -> JOIN,331,353,366,324,329,JOIN,331,353,366,324,329
  JOIN 0 (part all) -> PART,PART,PART

== KICK multi-target, explicitly ==
  KICK #x1,#x2 kb  -> 403
  KICK #x1 kb,kc   -> 441
  KICK #x1 kb      -> KICK