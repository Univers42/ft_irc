```python
#!/usr/bin/env python3
"""Section 7 — channel operator privilege matrix + per-feature checks."""
import sys
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = 6667
CH = "#ops"


def fresh():
    """alice op of #ops, bob plain member. Fresh channel each call."""
    global CH
    fresh.n += 1
    fresh.n += 1
    CH = "#ops%d" % fresh.n
    a = P.register(PORT, "al%d" % fresh.n)
    a.send("JOIN " + CH); a.collect()
    b = P.register(PORT, "bo%d" % fresh.n)
    b.send("JOIN " + CH); b.collect()
    a.collect(); a.buf = b""; b.buf = b""
    return a, b


fresh.n = 0

# NAMES prefix check
a, b = fresh()
a.send("NAMES " + CH); t = a.collect()
print("NAMES: %s" % t.strip().replace("\r\n", " | "))
print()

print("== 7.1 privilege matrix (bob = regular, alice = operator) ==")
print("%-3s %-28s %-22s %-22s" % ("#", "command", "as bob", "as alice"))
rows = [
    (1,  "KICK {c} {b} :bye",  "482", "KICK"),
    (2,  "INVITE carol {c}",   "482", "341"),
    (3,  "TOPIC {c} :new",     "482", "TOPIC"),
    (4,  "TOPIC {c}",          "331/332", "331/332"),
    (5,  "MODE {c} +i",        "482", "MODE"),
    (6,  "MODE {c} +t",        "482", "MODE"),
    (7,  "MODE {c} +k secret", "482", "MODE"),
    (8,  "MODE {c} +l 10",     "482", "MODE"),
    (9,  "MODE {c} +o {b}",    "482", "MODE"),
    (10, "MODE {c}",           "324", "324"),
    (11, "MODE {c} -o {a}",    "482", "MODE"),
    (12, "KICK {c} {a} :bye",  "482", "KICK"),
]


def summarise(t):
    nums = P.numerics(t)
    verbs = [l.split()[1] for l in t.split("\r\n") if l.startswith(":") and len(l.split()) > 1
             and l.split()[1].isalpha()]
    return ",".join(nums + verbs) or "(silence)"


for n, tmpl, exp_bob, exp_al in rows:
    a, b = fresh()
    carol = P.register(PORT, "carol%d" % fresh.n)
    cmd_b = tmpl.format(c=CH, b=b_nick, a=a_nick) if False else tmpl.format(
        c=CH, b="bo%d" % fresh.n, a="al%d" % fresh.n).replace("carol", "carol%d" % fresh.n)
    # as bob
    b.buf = b""
    b.send(cmd_b); tb = b.collect()
    # as alice (fresh pair so bob's failed attempt cannot interfere)
    a2, b2 = fresh()
    carol2 = P.register(PORT, "carol%d" % fresh.n)
    cmd_a = tmpl.format(c=CH, b="bo%d" % fresh.n, a="al%d" % fresh.n).replace("carol ", "carol%d " % fresh.n)
    a2.buf = b""
    a2.send(cmd_a); ta = a2.collect()
    print("%-3s %-28s %-22s %-22s" % (n, tmpl[:28], summarise(tb)[:22], summarise(ta)[:22]))
    for s in (a, b, carol, a2, b2, carol2):
        s.close()

```