```py
#!/usr/bin/env python3
"""Section 7.1 gated rows + 7.2 KICK / INVITE / TOPIC feature matrices."""
import sys
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = 6667
seq = [100]


def scene(modes=None, extra_join=True):
    seq[0] += 1
    n = seq[0]
    ch = "#f%d" % n
    a = P.register(PORT, "a%d" % n)
    a.send("JOIN " + ch); a.collect()
    if modes:
        a.send("MODE %s %s" % (ch, modes)); a.collect()
    b = P.register(PORT, "b%d" % n)
    if extra_join:
        b.send("JOIN " + ch); b.collect()
    c = P.register(PORT, "c%d" % n)
    a.collect()
    for s in (a, b, c):
        s.buf = b""
    return ch, a, b, c, "a%d" % n, "b%d" % n, "c%d" % n


def verbs(t):
    out = []
    for l in t.split("\r\n"):
        p = l.split()
        if len(p) > 1:
            out.append(p[1])
    return ",".join(out) or "(silence)"


print("== 7.1 gated rows, tested under the mode that gates them ==")
ch, a, b, c, an, bn, cn = scene("+i")
b.send("INVITE %s %s" % (cn, ch)); print("  row 2  INVITE as regular, channel +i  -> %s" % verbs(b.collect()))
a.buf = b""
a.send("INVITE %s %s" % (cn, ch)); print("  row 2  INVITE as operator, channel +i -> %s" % verbs(a.collect()))
for s in (a, b, c): s.close()

ch, a, b, c, an, bn, cn = scene("+t")
b.send("TOPIC %s :hijack" % ch); print("  row 3  TOPIC as regular, channel +t   -> %s" % verbs(b.collect()))
a.buf = b""
a.send("TOPIC %s :legit" % ch); print("  row 3  TOPIC as operator, channel +t  -> %s" % verbs(a.collect()))
for s in (a, b, c): s.close()

ch, a, b, c, an, bn, cn = scene()          # no +t
b.send("TOPIC %s :bob set this" % ch); print("  7.2-6  TOPIC as regular, channel -t   -> %s" % verbs(b.collect()))
for s in (a, b, c): s.close()

print("\n== 7.2 KICK ==")
kick_rows = [
    (1, "KICK {c} {b} :reason", "broadcast KICK"),
    (2, "KICK {c} {b}",         "works, default reason"),
    (3, "KICK {c} ghost :x",    "441"),
    (4, "KICK #nosuch {b} :x",  "403"),
    (6, "KICK",                 "461"),
]
for n, tmpl, exp in kick_rows:
    ch, a, b, c, an, bn, cn = scene()
    a.send(tmpl.format(c=ch, b=bn)); ta = a.collect(); tb = b.collect()
    print("%-3s %-26s exp=%-22s alice:%-18s bob:%s" % (n, tmpl[:26], exp, verbs(ta)[:18], verbs(tb)[:40]))
    if n == 2:
        print("       default reason line: %r" % [l for l in ta.split("\r\n") if "KICK" in l])
    for s in (a, b, c): s.close()

# 5 alice not in channel
ch, a, b, c, an, bn, cn = scene()
a.send("PART " + ch); a.collect(); a.buf = b""
a.send("KICK %s %s :x" % (ch, bn)); print("%-3s %-26s exp=%-22s -> %s" % (5, "KICK when not a member", "442", verbs(a.collect())))
for s in (a, b, c): s.close()

# 7 multi-channel, 8 multi-user
seq[0] += 1
n = seq[0]
a = P.register(PORT, "ma%d" % n); b = P.register(PORT, "mb%d" % n); d = P.register(PORT, "mc%d" % n)
a.send("JOIN #ma%d" % n, "JOIN #mb%d" % n); a.collect()
b.send("JOIN #ma%d" % n, "JOIN #mb%d" % n); b.collect()
d.send("JOIN #ma%d" % n); d.collect()
a.collect(); a.buf = b""
a.send("KICK #ma%d,#mb%d mb%d :x" % (n, n, n)); print("%-3s %-26s -> %s" % (7, "KICK #a,#b nick", verbs(a.collect())))
a.buf = b""
b.send("JOIN #ma%d" % n); b.collect(); a.collect(); a.buf = b""
a.send("KICK #ma%d mb%d,mc%d :x" % (n, n, n)); print("%-3s %-26s -> %s" % (8, "KICK #a nick1,nick2", verbs(a.collect())))
for s in (a, b, d): s.close()

print("\n== 7.2 INVITE ==")
inv_rows = [
    (1, "INVITE {carol} {c}", "341 + INVITE to carol"),
    (2, "INVITE {b} {c}",     "443 (already in)"),
    (3, "INVITE ghost {c}",   "401"),
    (4, "INVITE {carol} #nosuch", "403"),
    (5, "INVITE",             "461"),
]
for n, tmpl, exp in inv_rows:
    ch, a, b, c, an, bn, cn = scene()
    a.send(tmpl.format(c=ch, b=bn, carol=cn)); ta = a.collect(); tc = c.collect()
    print("%-3s %-26s exp=%-24s alice:%-12s carol:%s" % (n, tmpl[:26], exp, verbs(ta)[:12], verbs(tc)[:40]))
    for s in (a, b, c): s.close()

# 6 invited user joins +i, 7 uninvited
ch, a, b, c, an, bn, cn = scene("+i")
a.send("INVITE %s %s" % (cn, ch)); a.collect(); c.buf = b""
c.send("JOIN " + ch); print("%-3s %-26s exp=%-24s -> %s" % (6, "invited JOIN +i", "succeeds", verbs(c.collect())))
for s in (a, b, c): s.close()
ch, a, b, c, an, bn, cn = scene("+i")
c.send("JOIN " + ch); print("%-3s %-26s exp=%-24s -> %s" % (7, "uninvited JOIN +i", "473", verbs(c.collect())))
for s in (a, b, c): s.close()

print("\n== 7.2 TOPIC ==")
ch, a, b, c, an, bn, cn = scene()
a.send("TOPIC " + ch); print("%-3s %-30s -> %s" % (1, "TOPIC (none set)", verbs(a.collect())))
a.buf = b""
a.send("TOPIC %s :hello" % ch); ta = a.collect()
print("%-3s %-30s -> %s" % (2, "TOPIC :hello (broadcast)", verbs(ta)))
a.buf = b""
a.send("TOPIC " + ch); print("%-3s %-30s -> %s" % (3, "TOPIC read back", verbs(a.collect())))
a.buf = b""
a.send("TOPIC %s :" % ch); a.collect(); a.buf = b""
a.send("TOPIC " + ch); print("%-3s %-30s -> %s" % (4, "TOPIC : (clear) then read", verbs(a.collect())))
a.buf = b""
a.send("TOPIC #nosuch :x"); print("%-3s %-30s -> %s" % (7, "TOPIC #nosuch :x", verbs(a.collect())))
a.buf = b""
a.send("TOPIC %s :%s" % (ch, "T" * 400)); a.collect(); a.buf = b""
a.send("TOPIC " + ch); t = a.collect()
stored = [l for l in t.split("\r\n") if " 332 " in l]
print("%-3s %-30s -> stored topic length = %d (kTopicLen 390)" %
      (8, "400-char topic", len(stored[0].split(":", 2)[2]) if stored else -1))
for s in (a, b, c): s.close()

```




== 7.1 gated rows, tested under the mode that gates them ==
  row 2  INVITE as regular, channel +i  -> 442
  row 2  INVITE as operator, channel +i -> 341
  row 3  TOPIC as regular, channel +t   -> 482
  row 3  TOPIC as operator, channel +t  -> TOPIC
  7.2-6  TOPIC as regular, channel -t   -> TOPIC

== 7.2 KICK ==
1   KICK {c} {b} :reason       exp=broadcast KICK         alice:KICK               bob:KICK
2   KICK {c} {b}               exp=works, default reason  alice:KICK               bob:KICK
       default reason line: [':a105!a105@127.0.0.1 KICK #f105 b105 :a105']
3   KICK {c} ghost :x          exp=441                    alice:441                bob:(silence)
4   KICK #nosuch {b} :x        exp=403                    alice:403                bob:(silence)
6   KICK                       exp=461                    alice:461                bob:(silence)
5   KICK when not a member     exp=442                    -> 442
7   KICK #a,#b nick            -> 403
8   KICK #a nick1,nick2        -> 441

== 7.2 INVITE ==
1   INVITE {carol} {c}         exp=341 + INVITE to carol    alice:341          carol:INVITE
2   INVITE {b} {c}             exp=443 (already in)         alice:443          carol:(silence)
3   INVITE ghost {c}           exp=401                      alice:401          carol:(silence)
4   INVITE {carol} #nosuch     exp=403                      alice:403          carol:(silence)
5   INVITE                     exp=461                      alice:461          carol:(silence)
6   invited JOIN +i            exp=succeeds                 -> INVITE,JOIN,331,353,366,324,329
7   uninvited JOIN +i          exp=473                      -> 473

== 7.2 TOPIC ==
1   TOPIC (none set)               -> 331
2   TOPIC :hello (broadcast)       -> TOPIC
3   TOPIC read back                -> 332,333
4   TOPIC : (clear) then read      -> 331
7   TOPIC #nosuch :x               -> 403
8   400-char topic                 -> stored topic length = 390 (kTopicLen 390)
(timeout 5m)
