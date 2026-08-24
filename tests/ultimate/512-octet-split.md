```py
#!/usr/bin/env python3
"""8.7 row 5 (512-octet split) and 8.8 (each mode end to end)."""
import sys, os
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = int(os.environ.get("IRCPORT", 6767))
seq = [400]


def nick(p):
    seq[0] += 1
    return "%s%d" % (p, seq[0])


print("== 8.7 row 5 — a mode string whose echo would exceed 512 octets ==")
ch = "#split"
op = P.register(PORT, "spop")
op.send("JOIN " + ch); op.collect()
members = []
for i in range(60):
    n = "sp%02d" % i
    m = P.register(PORT, n)
    m.send("JOIN " + ch); m.collect()
    members.append((n, m))
op.collect(); op.buf = b""
big = "MODE %s +%s %s" % (ch, "o" * 60, " ".join(n for n, _ in members))
print("  sent: MODE %s +oooo...(60) <60 nicks>   (%d octets on the wire)" % (ch, len(big) + 2))
op.send(big)
t = op.collect(quiet=1.0, maxwait=6)
modelines = [l for l in t.split("\r\n") if " MODE " in l]
print("  MODE lines received: %d" % len(modelines))
for i, l in enumerate(modelines):
    print("    line %d: %d octets  %s..." % (i + 1, len(l) + 2, l[:72]))
print("  longest line (incl CRLF): %d  -> %s" %
      (max(len(l) + 2 for l in modelines) if modelines else 0,
       "OK (<=512)" if modelines and max(len(l) + 2 for l in modelines) <= 512 else "OVER 512"))
print("  numerics:", ",".join(P.numerics(t)) or "-")
for _, m in members:
    m.close()
op.close()

print("\n== 8.8 each mode end to end ==")


def scene(mode=None):
    a = P.register(PORT, nick("e"))
    c = "#e%d" % seq[0]
    a.send("JOIN " + c); a.collect()
    if mode:
        a.send("MODE %s %s" % (c, mode)); a.collect()
    a.buf = b""
    return a, c


def verbs(t):
    return ",".join(l.split()[1] for l in t.split("\r\n") if len(l.split()) > 1) or "(silence)"


# +i outsider
a, c = scene("+i")
o = P.register(PORT, nick("x")); o.send("JOIN " + c)
print("  +i  outsider JOIN            -> %s   (expect 473)" % verbs(o.collect()))
# +i after INVITE
a.buf = b""
a.send("INVITE %s %s" % (o_nick, c)) if False else None
inv = o
a.send("INVITE %s %s" % ("x%d" % seq[0], c)); a.collect(); inv.buf = b""
inv.send("JOIN " + c)
print("  +i  invited JOIN             -> %s   (expect JOIN)" % verbs(inv.collect()))
a.close(); o.close()

# +t
a, c = scene("+t")
b = P.register(PORT, nick("y")); b.send("JOIN " + c); b.collect(); b.buf = b""
b.send("TOPIC %s :x" % c)
print("  +t  non-op TOPIC             -> %s   (expect 482)" % verbs(b.collect()))
a.close(); b.close()

# +k
a, c = scene("+k secret")
k1 = P.register(PORT, nick("k")); k1.send("JOIN " + c)
print("  +k  JOIN with no key         -> %s   (expect 475)" % verbs(k1.collect()))
k2 = P.register(PORT, nick("k")); k2.send("JOIN %s wrong" % c)
print("  +k  JOIN with wrong key      -> %s   (expect 475)" % verbs(k2.collect()))
k3 = P.register(PORT, nick("k")); k3.send("JOIN %s secret" % c)
print("  +k  JOIN with correct key    -> %s   (expect JOIN)" % verbs(k3.collect()))
for s in (a, k1, k2, k3): s.close()

# +l
a, c = scene("+l 1")
l1 = P.register(PORT, nick("L")); l1.send("JOIN " + c)
print("  +l 1 second user JOIN        -> %s   (expect 471)" % verbs(l1.collect()))
a.close(); l1.close()

# +o / -o visible in the JOIN 353
a, c = scene()
b = P.register(PORT, nick("z")); bn = "z%d" % seq[0]
b.send("JOIN " + c); b.collect()
a.collect(); a.buf = b""
a.send("MODE %s +o %s" % (c, bn)); a.collect()
w = P.register(PORT, nick("w")); w.send("JOIN " + c); t = w.collect()
n353 = [l for l in t.split("\r\n") if " 353 " in l]
print("  +o  353 after JOIN           -> %s" % (n353[0].split(":")[-1] if n353 else "?"))
a.buf = b""
a.send("MODE %s -o %s" % (c, bn)); a.collect()
w2 = P.register(PORT, nick("w")); w2.send("JOIN " + c); t = w2.collect()
n353 = [l for l in t.split("\r\n") if " 353 " in l]
print("  -o  353 after JOIN           -> %s" % (n353[0].split(":")[-1] if n353 else "?"))
for s in (a, b, w, w2): s.close()

```


 Bash(cd /tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad
  sed -i 's/^a.send("INVITE %s %s" % (o_nick, c)) if False else None$//' t8b_endtoend.py
  timeout 600 python3 t8b_endtoend.py 2>&1; echo "exit=$?"; echo "--- server exits ---"; cat exits.log)
== 8.7 row 5 — a mode string whose echo would exceed 512 octets ==
  sent: MODE #split +oooo...(60) <60 nicks>   (375 octets on the wire)
  MODE lines received: 2
    line 1: 46 octets  :ft_irc 461 spop MODE :Not enough parameters...
    line 2: 108 octets  :spop!spop@127.0.0.1 MODE #split +oooooooooooo sp00 sp01 sp02 sp03 sp04 ...
  longest line (incl CRLF): 108  -> OK (<=512)
  numerics: 441,461

== 8.8 each mode end to end ==
  +i  outsider JOIN            -> 001,002,003,004,005,422,473   (expect 473)
  +i  invited JOIN             -> INVITE,JOIN,331,353,366,324,329   (expect JOIN)
  +t  non-op TOPIC             -> 482   (expect 482)
  +k  JOIN with no key         -> 001,002,003,004,005,422,475   (expect 475)
  +k  JOIN with wrong key      -> 001,002,003,004,005,422,475   (expect 475)
  +k  JOIN with correct key    -> 001,002,003,004,005,422,JOIN,331,353,366,324,329   (expect JOIN)
  +l 1 second user JOIN        -> 001,002,003,004,005,422,471   (expect 471)
  +o  353 after JOIN           -> @e411 @z412 w413
  -o  353 after JOIN           -> @e411 z412 w413 w414
exit=0
--- server exits ---
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 10m)