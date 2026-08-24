```py
python3 -c "
  import ircprobe as P
  print('empty username via double space: USER  0 * :A ->', P.numerics(P.oneshot(6667,'PASS pass','NICK ee','USER  0 * :A')))
  print()
  print('== 5.4 registration order ==')
  cases=[
   (1,'PASS,NICK,USER',['PASS pass','NICK o1','USER u 0 * :U'],'001'),
   (2,'PASS,USER,NICK',['PASS pass','USER u 0 * :U','NICK o2'],'001'),
   (3,'NICK,USER,PASS',['NICK o3','USER u 0 * :U','PASS pass'],'464'),
   (4,'NICK,PASS,USER',['NICK o4','PASS pass','USER u 0 * :U'],'464'),
   (5,'PASS,NICK only',['PASS pass','NICK o5'],'no 001'),
   (6,'PASS,USER only',['PASS pass','USER u 0 * :U'],'no 001'),
   (7,'PASS,NICK,USER,USER',['PASS pass','NICK o7','USER u 0 * :U','USER u 0 * :U'],'462'),
   (8,'PASS,NICK,NICK,USER',['PASS pass','NICK o8a','NICK o8b','USER u 0 * :U'],'001 as o8b'),
   (9,'JOIN before register',['PASS pass','NICK o9','JOIN #early'],'451'),
python3 -c "
  import ircprobe as P
  print('empty username via double space: USER  0 * :A ->', P.numerics(P.oneshot(6667,'PASS pass','NICK ee','USER  0 * :A')))
  print()
  print('== 5.4 registration order ==')
  cases=[
   (1,'PASS,NICK,USER',['PASS pass','NICK o1','USER u 0 * :U'],'001'),
   (2,'PASS,USER,NICK',['PASS pass','USER u 0 * :U','NICK o2'],'001'),
   (3,'NICK,USER,PASS',['NICK o3','USER u 0 * :U','PASS pass'],'464'),
   (4,'NICK,PASS,USER',['NICK o4','PASS pass','USER u 0 * :U'],'464'),
   (5,'PASS,NICK only',['PASS pass','NICK o5'],'no 001'),
   (6,'PASS,USER only',['PASS pass','USER u 0 * :U'],'no 001'),
   (7,'PASS,NICK,USER,USER',['PASS pass','NICK o7','USER u 0 * :U','USER u 0 * :U'],'462'),
   (8,'PASS,NICK,NICK,USER',['PASS pass','NICK o8a','NICK o8b','USER u 0 * :U'],'001 as o8b'),
   (9,'JOIN before register',['PASS pass','NICK o9','JOIN #early'],'451'),
   (10,'QUIT half-registered',['PASS pass','NICK o10','QUIT :bye'],'clean close'),
  ]
  for n,d,lines,exp in cases:
      t=P.oneshot(6667,*lines)
      print('%-3s %-24s exp=%-14s got=%-30s' % (n,d,exp,','.join(P.numerics(t)) or '(silence)'), end='')
      if n==8:
          import re; m=re.search(r' 001 (\S+)',t); print(' nick=%s'%(m.group(1) if m else '?'), end='')
      print()
  ")
empty username via double space: USER  0 * :A -> ['461']

== 5.4 registration order ==
1   PASS,NICK,USER           exp=001            got=001,002,003,004,005,422
2   PASS,USER,NICK           exp=001            got=001,002,003,004,005,422
3   NICK,USER,PASS           exp=464            got=464
4   NICK,PASS,USER           exp=464            got=001,002,003,004,005,422
5   PASS,NICK only           exp=no 001         got=(silence)
6   PASS,USER only           exp=no 001         got=(silence)
7   PASS,NICK,USER,USER      exp=462            got=001,002,003,004,005,422,462
8   PASS,NICK,NICK,USER      exp=001 as o8b     got=001,002,003,004,005,422        nick=o8b
9   JOIN before register     exp=451            got=451
10  QUIT half-registered     exp=clean close    got=(silence)
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 2m)

```