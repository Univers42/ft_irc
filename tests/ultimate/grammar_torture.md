```py
#!/usr/bin/env python3
"""Section 9 — grammar torture: message signature, framing, octet abuse."""
import sys, os, socket, time
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = int(os.environ.get("IRCPORT", 6767))
seq = [600]


def n():
    seq[0] += 1
    return "g%d" % seq[0]


def after_reg(*lines, **kw):
    s = P.Session(PORT)
    s.send("PASS pass", "NICK %s" % n(), "USER u 0 * :U")
    s.collect()
    s.buf = b""
    for l in lines:
        s.send_raw(l)
    t = s.collect(quiet=kw.get("quiet", 0.4))
    s.close()
    return t


def summ(t):
    if not t.strip():
        return "(silence)"
    return ",".join(l.split()[1] for l in t.split("\r\n") if len(l.split()) > 1)


print("== 9.0 A — command is 1*letter or exactly 3digit ==")
for cmd, exp in [("PING :x", "PONG"), ("ping :x", "PONG"), ("PINGPINGPING", "421"),
                 ("421", "silence"), ("12", "silence"), ("1234", "silence"),
                 ("A1", "silence"), ("PING1", "silence"), ("+PING", "silence"), ("", "silence")]:
    t = after_reg(cmd + "\r\n")
    print("  %-14s exp=%-9s got=%s" % (repr(cmd), exp, summ(t)))

print("\n== 9.0 B — middle may contain ':' but not lead with it ==")
a = P.register(PORT, "bx1")
a.send("JOIN #ab"); a.collect(); a.buf = b""
a.send("PRIVMSG a:b :hi"); print("  PRIVMSG a:b :hi        ->", summ(a.collect()))
a.buf = b""
a.send("MODE #a:b +i"); print("  MODE #a:b +i           ->", summ(a.collect()))
a.close()
print("  USER a:b 0 * :R        -> (verified accepted in 5.3.1)")

print("\n== 9.0 C — the 15th parameter's colon is optional ==")
mid = " ".join("p%d" % i for i in range(14))
t = after_reg("PRIVMSG #g %s tail with spaces\r\n" % mid)
print("  14 middles + bare trailing -> %s" % summ(t))
t = after_reg("PRIVMSG #g p0 p1 p2 :trailing here\r\n")
print("  3 middles + :trailing      -> %s" % summ(t))

print("\n== 9.0 E — a client prefix is accepted and ignored ==")
for line, exp in [(":nick PING :x", "PONG"), (":nick!user@host PING :x", "PONG"),
                  (": PING :x", "421/ignored"), (":only-a-prefix", "ignored")]:
    t = after_reg(line + "\r\n")
    print("  %-26s exp=%-12s got=%s" % (line, exp, summ(t)))

# impersonation
al = P.register(PORT, "spoofa"); ml = P.register(PORT, "mallory")
al.send("JOIN #spoof"); al.collect()
ml.send("JOIN #spoof"); ml.collect()
al.collect(); al.buf = b""
ml.send(":spoofa PRIVMSG #spoof :I am alice"); ml.collect()
t = al.collect()
print("  impersonation attempt      -> relayed as: %s" % (t.strip().split(" ")[0] if t.strip() else "(nothing)"))
al.close(); ml.close()

print("\n== 9.1 message framing ==")
rows = [("PING :tok", "PONG :tok"), ("ping :tok", "PONG"), ("PING", "409"),
        ("", "ignored"), ("   ", "ignored/421"), (":prefix PING :tok", "PONG"),
        ("NOTACOMMAND", "421"), ("123", "421/ignored"), ("12", "421-or-silence"),
        ("PING|extra", "421-or-silence")]
for cmd, exp in rows:
    t = after_reg(cmd + "\r\n")
    print("  %-20s exp=%-16s got=%s" % (repr(cmd), exp, summ(t)))

# 11 sixteen parameters
t = after_reg("PRIVMSG #g " + " ".join("q%d" % i for i in range(16)) + "\r\n")
print("  16 parameters        -> %s" % summ(t))

# 12/13 line length
s = P.Session(PORT)
s.send("PASS pass", "NICK len1", "USER u 0 * :U"); s.collect(); s.buf = b""
s.send("JOIN #len"); s.collect(); s.buf = b""
exact = "PRIVMSG #len :" + "A" * (510 - len("PRIVMSG #len :"))
s.send_raw(exact + "\r\n")
print("  512-octet line       -> %s (len sent=%d)" % (summ(s.collect()), len(exact) + 2))
s.buf = b""
s.send_raw("PRIVMSG #len :" + "B" * 600 + "\r\nJOIN #smuggled\r\n")
t = s.collect()
print("  513+ line then JOIN  -> %s" % summ(t))
print("     smuggled JOIN executed as its own command? %s" %
      ("YES (correct)" if "JOIN" in t or "353" in t else "no"))
s.close()

print("\n== 9.2 octet-level abuse ==")


def raw(desc, payload, expect, quiet=0.5):
    s = P.Session(PORT)
    try:
        s.send_raw(payload)
        t = s.collect(quiet=quiet)
    except OSError as e:
        t = "(socket error: %s)" % e
    s.close()
    print("  %-34s exp=%-18s got=%s" % (desc, expect, summ(t) if isinstance(t, str) else t))


raw("1 NUL in nick", b"PASS pass\r\nNICK a\x00b\r\nUSER u 0 * :U\r\n", "no crash")
raw("2 bare CR, no LF", b"PASS pass\r\nNICK cr1\r\nUSER u 0 * :U\r\nPING :x\r", "buffered")
raw("3 bare LF, no CR", b"PASS pass\nNICK lf1\nUSER u 0 * :U\nPING :x\n", "accepted")
raw("4 reversed \\n\\r", b"PASS pass\n\rNICK nr1\n\rUSER u 0 * :U\n\r", "two terminators")
raw("5 high-bit nick", b"PASS pass\r\nNICK \xff\xfe\r\nUSER u 0 * :U\r\n", "432")
raw("6 UTF-8 realname", "PASS pass\r\nNICK utf1\r\nUSER u 0 * :caf\u00e9 \u00e9t\u00e9\r\n".encode("utf-8"),
    "accepted")
raw("9 ':' alone", b"PASS pass\r\nNICK col1\r\nUSER u 0 * :U\r\n:\r\n", "ignored/421")
raw("10 400x %s in PRIVMSG", b"PASS pass\r\nNICK fmt1\r\nUSER u 0 * :U\r\nJOIN #f\r\nPRIVMSG #f :" +
    b"%s" * 200 + b"\r\n", "verbatim")

# 7 an 8 KiB single line
s = P.Session(PORT)
s.send("PASS pass", "NICK big8", "USER u 0 * :U"); s.collect(); s.buf = b""
s.send_raw("PRIVMSG #x :" + "Z" * 8192 + "\r\nPING :alive\r\n")
t = s.collect()
print("  %-34s exp=%-18s got=%s" % ("7 8 KiB single line", "truncated, alive", summ(t)))
s.close()

# 8 a megabyte with no terminator
import subprocess
rss_before = subprocess.check_output(
    "ps -o rss= -C ircserv.snapshot | head -1", shell=True).decode().strip()
s = P.Session(PORT, timeout=2)
try:
    s.send_raw(b"A" * 1048576)
    time.sleep(1.0)
    t = s.collect(quiet=0.5)
except OSError as e:
    t = "(disconnected: %s)" % e
s.close()
time.sleep(0.5)
rss_after = subprocess.check_output(
    "ps -o rss= -C ircserv.snapshot | head -1", shell=True).decode().strip()
print("  %-34s exp=%-18s got=%s | RSS %s -> %s KiB" %
      ("8 1 MiB, no terminator", "no OOM", summ(t) if isinstance(t, str) else t, rss_before, rss_after))

print("\n== after every one of these: is the server alive? ==")
t = P.reg_then(PORT, "alive9", "JOIN #eval", "PRIVMSG #eval :still here")
print("  new client registers and joins ->", summ(t))

```


sh(cd /tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad && timeout 600 python3 t9_grammar.py 2>&1; echo "exit=$?"; echo "--- server exits ---"; cat exits.log)
== 9.0 A — command is 1*letter or exactly 3digit ==
  'PING :x'      exp=PONG      got=PONG
  'ping :x'      exp=PONG      got=PONG
  'PINGPINGPING' exp=421       got=421
  '421'          exp=silence   got=421
  '12'           exp=silence   got=(silence)
  '1234'         exp=silence   got=(silence)
  'A1'           exp=silence   got=(silence)
  'PING1'        exp=silence   got=(silence)
  '+PING'        exp=silence   got=(silence)
  ''             exp=silence   got=(silence)

== 9.0 B — middle may contain ':' but not lead with it ==
  PRIVMSG a:b :hi        -> 401
  MODE #a:b +i           -> 403
  USER a:b 0 * :R        -> (verified accepted in 5.3.1)

== 9.0 C — the 15th parameter's colon is optional ==
  14 middles + bare trailing -> 403
  3 middles + :trailing      -> 403

== 9.0 E — a client prefix is accepted and ignored ==
  :nick PING :x              exp=PONG         got=PONG
  :nick!user@host PING :x    exp=PONG         got=PONG
  : PING :x                  exp=421/ignored  got=(silence)
  :only-a-prefix             exp=ignored      got=(silence)
  impersonation attempt      -> relayed as: :mallory!mallory@127.0.0.1
  : PING :x                  exp=421/ignored  got=(silence)
  :only-a-prefix             exp=ignored      got=(silence)
  impersonation attempt      -> relayed as: :mallory!mallory@127.0.0.1

== 9.1 message framing ==
  'PING :tok'          exp=PONG :tok        got=PONG
  'ping :tok'          exp=PONG             got=PONG
  'PING'               exp=409              got=409
  ''                   exp=ignored          got=(silence)
  '   '                exp=ignored/421      got=(silence)
  ':prefix PING :tok'  exp=PONG             got=PONG
  'NOTACOMMAND'        exp=421              got=421
  '123'                exp=421/ignored      got=421
  '12'                 exp=421-or-silence   got=(silence)
  'PING|extra'         exp=421-or-silence   got=(silence)
  16 parameters        -> 403
  512-octet line       -> (silence) (len sent=512)
  513+ line then JOIN  -> JOIN,331,353,366,324,329
     smuggled JOIN executed as its own command? YES (correct)

== 9.2 octet-level abuse ==
  1 NUL in nick                      exp=no crash           got=001,002,003,004,005,422
  2 bare CR, no LF                   exp=buffered           got=001,002,003,004,005,422
  3 bare LF, no CR                   exp=accepted           got=001,002,003,004,005,422,PONG
  4 reversed \n\r                    exp=two terminators    got=001,002,003,004,005,422
  5 high-bit nick                    exp=432                got=432
  6 UTF-8 realname                   exp=accepted           got=001,002,003,004,005,422
  9 ':' alone                        exp=ignored/421        got=001,002,003,004,005,422
  10 400x %s in PRIVMSG              exp=verbatim           got=001,002,003,004,005,422,JOIN,331,353,366,324,329
  7 8 KiB single line                exp=truncated, alive   got=403,PONG
  8 1 MiB, no terminator             exp=no OOM             got=451 | RSS 4528 -> 4528 KiB

== after every one of these: is the server alive? ==
  new client registers and joins -> JOIN,331,353,366,324,329
exit=0
--- server exits ---
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 10m)

● Bash(cd /tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad
  timeout 300 python3 -c "
  import sys; sys.path.insert(0,'.')
  import ircprobe as P
  PORT=6767

  print('== PONG token echo ==')
  print(' ', P.reg_then(PORT,'pt1','PING :tok').strip())
  print(' ', P.reg_then(PORT,'pt2','PING abc').strip())

  print()
  print('== 9.0 B with targets that really exist ==')
  a=P.register(PORT,'acolon'); a.send('JOIN #real'); a.collect()
  # a user whose nick cannot contain ':' -> use a channel with ':' instead
  print('  JOIN #a:b        ->', P.reg_then(PORT,'jc1','JOIN #a:b').strip()[:90])
  b=P.register(PORT,'bcolon')
  a.buf=b''
  # PRIVMSG to an existing nick, target token written with a colon inside is a DIFFERENT nick
  print('  PRIVMSG bcolon:x ->', P.reg_then(PORT,'jc2','PRIVMSG bcolon:x :hi').strip()[:90])
  a.close(); b.close()

  print()
  print('== 9.0 C — trailing rules, on a channel that exists ==')
  r=P.register(PORT,'rcv'); r.send('JOIN #tc'); r.collect()
  s=P.register(PORT,'snd'); s.send('JOIN #tc'); s.collect(); r.collect(); r.buf=b''
  mid=' '.join('p%d'%i for i in range(12))
  s.send('PRIVMSG #tc %s tail with spaces'%mid)   # 14 middles total then bare tail
  t=r.collect(); print('  14 middles + bare tail ->', t.strip()[:150])
  r.buf=b''
  s.send('PRIVMSG #tc p0 p1 bare words here')     # under 14: bare word is just a middle
  t=r.collect(); print('  few middles + bare tail->', t.strip()[:150])
  r.buf=b''
  s.send('PRIVMSG #tc :colon trailing here')
  t=r.collect(); print('  :trailing              ->', t.strip()[:150])
  r.buf=b''
  s.send('PRIVMSG #tc '+' '.join('q%d'%i for i in range(16)))
  t=r.collect(); print('  16 params              ->', t.strip()[:150])
  r.close(); s.close()

  print()
  print('== 9.2 row 1 — what nick results from NICK a<NUL>b ==')
  s=P.Session(PORT); s.send_raw(b'PASS pass\r\nNICK a\x00b\r\nUSER u 0 * :U\r\n'); t=s.collect()
  import re; m=re.search(r' 001 (\S+)',t)
  print('  registered nick = %r' % (m.group(1) if m else None), '| numerics', P.numerics(t))
  s.close()
  " 2>&1)
== PONG token echo ==
  :ft_irc PONG ft_irc :tok
  :ft_irc PONG ft_irc :abc

== 9.0 B with targets that really exist ==
  JOIN #a:b        -> :ft_irc 476 jc1 #a:b :Bad Channel Mask
  PRIVMSG bcolon:x -> :ft_irc 401 jc2 bcolon:x :No such nick/channel

== 9.0 C — trailing rules, on a channel that exists ==
  14 middles + bare tail -> :snd!snd@127.0.0.1 PRIVMSG #tc :p0 p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 tail with spaces
  few middles + bare tail-> :snd!snd@127.0.0.1 PRIVMSG #tc :p0 p1 bare words here
  :trailing              -> :snd!snd@127.0.0.1 PRIVMSG #tc :colon trailing here
  16 params              -> :snd!snd@127.0.0.1 PRIVMSG #tc :q0 q1 q2 q3 q4 q5 q6 q7 q8 q9 q10 q11 q12 q13 q14 q15

== 9.2 row 1 — what nick results from NICK a<NUL>b ==
  registered nick = 'ab' | numerics ['001', '002', '003', '004', '005', '422']
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 5m)