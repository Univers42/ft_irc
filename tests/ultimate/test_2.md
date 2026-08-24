```python
#!/usr/bin/env python3
"""Section 6 — PRIVMSG / NOTICE matrix, with a real second client."""
import sys
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = 6667
res = []


def rec(n, cmd, expect, got, detail=""):
    res.append((n, cmd, expect, got, detail))


alice = P.register(PORT, "alice")
bob = P.register(PORT, "bob")
alice.send("JOIN #eval"); alice.collect()
bob.send("JOIN #eval"); bob.collect()
alice.buf = b""; bob.buf = b""

# 1 user target
alice.send("PRIVMSG bob :hello"); alice.collect(); t = bob.collect()
rec(1, "PRIVMSG bob :hello", "bob gets it", "PRIVMSG bob :hello" in t, t.strip())
bob.buf = b""; alice.buf = b""

# 2 channel target: every member except the sender
alice.send("PRIVMSG #eval :hello"); ta = alice.collect(); tb = bob.collect()
rec(2, "PRIVMSG #eval :hello", "bob yes / alice no echo",
    ("hello" in tb, "hello" in ta), "bob=%r alice=%r" % (tb.strip(), ta.strip()))
bob.buf = b""; alice.buf = b""

# 3 no colon, one word
alice.send("PRIVMSG bob hello"); alice.collect(); t = bob.collect()
rec(3, "PRIVMSG bob hello", "delivered", "hello" in t, t.strip())
bob.buf = b""; alice.buf = b""

# 4 spaces preserved
alice.send("PRIVMSG bob :hi there you"); alice.collect(); t = bob.collect()
rec(4, "PRIVMSG bob :hi there you", "spaces kept", "hi there you" in t, t.strip())
bob.buf = b""; alice.buf = b""

# 5 casemapping
alice.send("PRIVMSG BOB :hi"); alice.collect(); t = bob.collect()
rec(5, "PRIVMSG BOB :hi", "delivered to bob", "PRIVMSG bob :hi" in t or "hi" in t, t.strip())
bob.buf = b""; alice.buf = b""

# 6-10 error replies
for n, cmd, exp in [(6, "PRIVMSG nosuch :hi", "401"),
                    (7, "PRIVMSG #nosuch :hi", "401/403"),
                    (8, "PRIVMSG", "411"),
                    (9, "PRIVMSG bob", "412"),
                    (10, "PRIVMSG #eval :", "412")]:
    alice.buf = b""
    alice.send(cmd); t = alice.collect()
    rec(n, cmd, exp, ",".join(P.numerics(t)) or "(silence)", t.strip())

# 11 oversize
alice.buf = b""
alice.send("PRIVMSG #eval :" + "X" * 500); alice.collect(); t = bob.collect()
longest = max((len(l) for l in t.split("\r\n") if l), default=0)
rec(11, "PRIVMSG #eval :<500 X>", "delivered, line <=510+CRLF", "%d octets (no CRLF)" % longest,
    "payload X count=%d" % t.count("X"))
bob.buf = b""; alice.buf = b""

# 12 self-message
alice.send("PRIVMSG alice :self"); t = alice.collect()
rec(12, "PRIVMSG alice :self", "alice receives it", "PRIVMSG alice :self" in t, t.strip())
alice.buf = b""

# 13 not a member of an existing channel
carol = P.register(PORT, "carol")
carol.send("PRIVMSG #eval :hi"); t = carol.collect()
rec(13, "PRIVMSG #eval :hi (not joined)", "404", ",".join(P.numerics(t)) or "(silence)", t.strip())

# NOTICE: never an automatic error
carol.buf = b""
carol.send("NOTICE ghost :hi"); t = carol.collect()
rec("N1", "NOTICE ghost :hi", "silence (no 401)", ",".join(P.numerics(t)) or "(silence)", t.strip())
carol.buf = b""
carol.send("NOTICE", "NOTICE bob", "NOTICE #nosuch :x"); t = carol.collect()
rec("N2", "NOTICE (no params / no text / bad chan)", "silence", ",".join(P.numerics(t)) or "(silence)", t.strip())
alice.send("PRIVMSG alice :self"); t = alice.collect()
rec(12, "PRIVMSG alice :self", "alice receives it", "PRIVMSG alice :self" in t, t.strip())
alice.buf = b""
alice.send("PRIVMSG #eval :" + "X" * 500); alice.collect(); t = bob.collect()
longest = max((len(l) for l in t.split("\r\n") if l), default=0)
rec(11, "PRIVMSG #eval :<500 X>", "delivered, line <=510+CRLF", "%d octets (no CRLF)" % longest,
    "payload X count=%d" % t.count("X"))
bob.buf = b""; alice.buf = b""

# 12 self-message
alice.send("PRIVMSG alice :self"); t = alice.collect()
rec(12, "PRIVMSG alice :self", "alice receives it", "PRIVMSG alice :self" in t, t.strip())
alice.buf = b""

# 13 not a member of an existing channel
carol = P.register(PORT, "carol")
carol.send("PRIVMSG #eval :hi"); t = carol.collect()
rec(13, "PRIVMSG #eval :hi (not joined)", "404", ",".join(P.numerics(t)) or "(silence)", t.strip())

# NOTICE: never an automatic error
carol.buf = b""
carol.send("NOTICE ghost :hi"); t = carol.collect()
rec("N1", "NOTICE ghost :hi", "silence (no 401)", ",".join(P.numerics(t)) or "(silence)", t.strip())
carol.buf = b""
carol.send("NOTICE", "NOTICE bob", "NOTICE #nosuch :x"); t = carol.collect()
rec("N2", "NOTICE (no params / no text / bad chan)", "silence", ",".join(P.numerics(t)) or "(silence)", t.strip())

print("%-4s %-34s %-24s %-34s %s" % ("#", "command", "expected", "got", "detail"))
for n, c, e, g, d in res:
    print("%-4s %-34s %-24s %-34s %s" % (n, str(c)[:34], e, str(g)[:34], str(d)[:110]))

for s in (alice, bob, carol):
    s.close()



```