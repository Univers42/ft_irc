```python
#!/usr/bin/env python3
"""Section 11 — bonus FILE transfer: happy path + failure matrix."""
import sys, os, base64, time
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = int(os.environ.get("IRCPORT", 6767))
seq = [800]


def n(p):
    seq[0] += 1
    return "%s%d" % (p, seq[0])


def lines(t):
    return [l for l in t.split("\r\n") if l]


print("== 11 happy path ==")
an, bn = n("al"), n("bo")
a = P.register(PORT, an)
b = P.register(PORT, bn)
a.buf = b""; b.buf = b""

payload = b"hello world!"          # 12 octets
chunk = base64.b64encode(payload).decode()
a.send("FILE SEND %s notes.txt %d" % (bn, len(payload)))
ta = a.collect(); tb = b.collect()
print("  1 alice: FILE SEND        -> %s" % (lines(ta) or "(silence)"))
print("    bob receives            -> %s" % (lines(tb) or "(silence)"))

tid = None
for l in lines(tb):
    if "FILE OFFER" in l:
        tid = l.split("FILE OFFER ")[1].split()[0]
print("    transfer id = %r" % tid)

a.buf = b""; b.buf = b""
b.send("FILE ACCEPT %s" % tid)
tb = b.collect(); ta = a.collect()
print("  2 bob: FILE ACCEPT        -> bob:%s  alice:%s" % (lines(tb) or "-", lines(ta) or "-"))

a.buf = b""; b.buf = b""
a.send("FILE DATA %s %s" % (tid, chunk))
ta = a.collect(); tb = b.collect()
print("  3 alice: FILE DATA        -> bob:%s" % (lines(tb) or "-"))
got = None
for l in lines(tb):
    if "FILE DATA" in l:
        got = l.split()[-1]
print("    decoded payload = %r  (matches: %s)" %
      (base64.b64decode(got) if got else None, base64.b64decode(got) == payload if got else False))

a.buf = b""; b.buf = b""
a.send("FILE END %s" % tid)
ta = a.collect(); tb = b.collect()
print("  4 alice: FILE END         -> bob:%s  alice:%s" % (lines(tb) or "-", lines(ta) or "-"))
a.close(); b.close()

print("\n== 11.1 failure matrix ==")


def pair():
    x, y = n("s"), n("r")
    sa = P.register(PORT, x)
    sb = P.register(PORT, y)
    sa.buf = b""; sb.buf = b""
    return sa, sb, x, y


rows = [
    (1,  "FILE SEND ghost f.txt 10",        "no such nick"),
    (2,  "FILE SEND SELF f.txt 10",         "cannot send to yourself"),
    (3,  "FILE SEND PEER ../etc/passwd 10", "invalid filename"),
    (4,  "FILE SEND PEER a b.txt 10",       "invalid filename / arity"),
    (5,  "FILE SEND PEER a,b.txt 10",       "invalid filename"),
    (6,  "FILE SEND PEER f.txt 0",          "invalid size"),
    (7,  "FILE SEND PEER f.txt 99999999999", "invalid size"),
    (31, "FILE SEND PEER . 10",             "invalid filename"),
    (32, "FILE SEND PEER .. 10",            "invalid filename"),
]
for num, tmpl, exp in rows:
    sa, sb, x, y = pair()
    sa.send(tmpl.replace("SELF", x).replace("PEER", y))
    t = sa.collect()
    print("  %-3s %-34s exp=%-26s got=%s" % (num, tmpl[:34], exp, (lines(t) or ["(silence)"])[0][:70]))
    sa.close(); sb.close()

# 8-12 need a live transfer
sa, sb, x, y = pair()
sa.send("FILE SEND %s ok.txt 12" % y); sa.collect(); tb = sb.collect()
tid = [l.split("FILE OFFER ")[1].split()[0] for l in lines(tb) if "FILE OFFER" in l][0]
sb.buf = b""; sa.buf = b""
sa.send("FILE DATA %s QUJD" % tid)
print("  11  DATA before ACCEPT             exp=%-26s got=%s" %
      ("not accepted yet", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.buf = b""
sb.send("FILE ACCEPT %s" % tid); sb.collect(); sa.collect(); sa.buf = b""
sa.send("FILE DATA %s not*base64!" % tid)
print("  8   malformed base64               exp=%-26s got=%s" %
      ("aborted", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.close(); sb.close()

sa, sb, x, y = pair()
sa.send("FILE SEND %s ok.txt 12" % y); sa.collect(); tb = sb.collect()
tid = [l.split("FILE OFFER ")[1].split()[0] for l in lines(tb) if "FILE OFFER" in l][0]
sb.send("FILE ACCEPT %s" % tid); sb.collect(); sa.collect(); sa.buf = b""
sa.send("FILE DATA %s QQ" % tid)
print("  9   base64 not a multiple of 4     exp=%-26s got=%s" %
      ("aborted", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.close(); sb.close()

sa, sb, x, y = pair()
sa.send("FILE DATA 999 QUJD")
print("  10  unknown transfer id            exp=%-26s got=%s" %
      ("no transfer with id", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.close(); sb.close()

# 12 more bytes than declared
sa, sb, x, y = pair()
sa.send("FILE SEND %s ok.txt 4" % y); sa.collect(); tb = sb.collect()
tid = [l.split("FILE OFFER ")[1].split()[0] for l in lines(tb) if "FILE OFFER" in l][0]
sb.send("FILE ACCEPT %s" % tid); sb.collect(); sa.collect(); sa.buf = b""
sa.send("FILE DATA %s %s" % (tid, base64.b64encode(b"way too many bytes").decode()))
print("  12  size overrun                   exp=%-26s got=%s" %
      ("size overrun, aborted", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.close(); sb.close()

# 11.2 row 5 — two transfers to the same pair
sa, sb, x, y = pair()
sa.send("FILE SEND %s one.txt 10" % y); sa.collect(); sa.buf = b""
sa.send("FILE SEND %s two.txt 10" % y)
print("\n  11.2-5 second transfer, same pair  exp=%-24s got=%s" %
      ("already active", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.close(); sb.close()

# 11.2 row 2/3 — peer disconnects mid-transfer
sa, sb, x, y = pair()
sa.send("FILE SEND %s mid.txt 12" % y); sa.collect(); tb = sb.collect()
tid = [l.split("FILE OFFER ")[1].split()[0] for l in lines(tb) if "FILE OFFER" in l][0]
sb.send("FILE ACCEPT %s" % tid); sb.collect(); sa.collect(); sa.buf = b""
sb.close()
time.sleep(0.8)
print("  11.2-2 recipient disconnects       exp=%-24s got=%s" %
      ("FILE ABRT to sender", (lines(sa.collect()) or ["(silence)"])[0][:70]))
sa.close()

print("\n== the bot (bonus) ==")
bt = P.register(PORT, n("bt"))
for cmd in ["!help", "!time", "!info", "!joke"]:
    bt.buf = b""
    bt.send("PRIVMSG ircbot :%s" % cmd)
    print("  %-6s -> %s" % (cmd, (lines(bt.collect()) or ["(silence)"])[0][:100]))
bt.buf = b""
bt.send("JOIN #botch"); bt.collect(); bt.buf = b""
bt.send("PRIVMSG #botch :!help")
print("  !help in a channel -> %s  (bot answers private messages only)" %
      (lines(bt.collect()) or ["(silence)"]))
bt.buf = b""
bt.send("NICK ircbot")
print("  taking the nick 'ircbot' -> %s" % (lines(bt.collect()) or ["(silence)"]))
bt.close()


```


== 11 happy path ==
  1 alice: FILE SEND        -> [':ft_irc NOTICE al801 :FILE 1 offered to bo802']
    bob receives            -> [':al801!al801@127.0.0.1 FILE OFFER 1 notes.txt 12']
    transfer id = '1'
  2 bob: FILE ACCEPT        -> bob:-  alice:[':bo802!bo802@127.0.0.1 FILE OK 1']
  3 alice: FILE DATA        -> bob:[':al801!al801@127.0.0.1 FILE DATA 1 aGVsbG8gd29ybGQh']
    decoded payload = b'hello world!'  (matches: True)
  4 alice: FILE END         -> bob:[':al801!al801@127.0.0.1 FILE END 1 12']  alice:-

== 11.1 failure matrix ==
  1   FILE SEND ghost f.txt 10           exp=no such nick               got=:ft_irc NOTICE s803 :FILE: no such nick ghost
  2   FILE SEND SELF f.txt 10            exp=cannot send to yourself    got=:ft_irc NOTICE s805 :FILE: cannot send to yourself
  3   FILE SEND PEER ../etc/passwd 10    exp=invalid filename           got=:ft_irc NOTICE s807 :FILE: invalid filename
  4   FILE SEND PEER a b.txt 10          exp=invalid filename / arity   got=:ft_irc NOTICE s809 :FILE: invalid size (1..52428800)
  5   FILE SEND PEER a,b.txt 10          exp=invalid filename           got=:ft_irc NOTICE s811 :FILE: invalid filename
  6   FILE SEND PEER f.txt 0             exp=invalid size               got=:ft_irc NOTICE s813 :FILE: invalid size (1..52428800)
  7   FILE SEND PEER f.txt 99999999999   exp=invalid size               got=:ft_irc NOTICE s815 :FILE: invalid size (1..52428800)
  31  FILE SEND PEER . 10                exp=invalid filename           got=:ft_irc NOTICE s817 :FILE: invalid filename
  32  FILE SEND PEER .. 10               exp=invalid filename           got=:ft_irc NOTICE s819 :FILE: invalid filename
  11  DATA before ACCEPT             exp=not accepted yet           got=:ft_irc NOTICE s821 :FILE: transfer 2 not accepted yet
  8   malformed base64               exp=aborted                    got=:ft_irc NOTICE s821 :FILE ABRT 2 :malformed chunk
  9   base64 not a multiple of 4     exp=aborted                    got=:ft_irc NOTICE s823 :FILE ABRT 3 :malformed chunk
  10  unknown transfer id            exp=no transfer with id        got=:ft_irc NOTICE s825 :FILE: no transfer with id 999
  12  size overrun                   exp=size overrun, aborted      got=:ft_irc NOTICE s827 :FILE ABRT 4 :size overrun

  11.2-5 second transfer, same pair  exp=already active           got=:ft_irc NOTICE s829 :FILE: a transfer to r830 is already active
  11.2-2 recipient disconnects       exp=FILE ABRT to sender      got=:ft_irc NOTICE s831 :FILE ABRT 6 :peer disconnected

== the bot (bonus) ==
  !help  -> :ircbot PRIVMSG bt833 :Available commands:
  !time  -> :ircbot PRIVMSG bt833 :Server time: 2026-08-24 03:44:44
  !info  -> :ircbot PRIVMSG bt833 :Server: ft_irc v1.0
  !joke  -> :ircbot PRIVMSG bt833 :Why do programmers prefer dark mode? Because light attracts bugs.
  !help in a channel -> ['(silence)']  (bot answers private messages only)
  taking the nick 'ircbot' -> [':ft_irc 433 bt833 ircbot :Nickname is already in use']
exit=0
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 10m)

● Bash(timeout 1800 make test 2>&1 | tail -25)
