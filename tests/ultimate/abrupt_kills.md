```py
#!/usr/bin/env python3
"""Section 4 — networking specials: partial commands, abrupt kill, framing table."""
import sys, os, time, socket, threading
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = int(os.environ.get("IRCPORT", 6767))


def summ(t):
    return ",".join(l.split()[1] for l in t.split("\r\n") if len(l.split()) > 1) or "(silence)"


print("== 4.1 partial command, dribbled out, while another client stays live ==")
slow = P.Session(PORT, timeout=1.0)
slow.send_raw("PASS pass\r\nNICK ali")
t0 = time.time()

responsive = []


def poke():
    """Runs while `slow` is mid-command: proves the server is not blocked."""
    for i in range(3):
        time.sleep(0.6)
        t = P.reg_then(PORT, "live%d" % i, "JOIN #eval", "PRIVMSG #eval :still alive")
        responsive.append("001" in t or "JOIN" in t or t != "")


th = threading.Thread(target=poke)
th.start()
time.sleep(1.0)
print("  after 'NICK ali' with no CRLF, received: %r" % slow.collect(quiet=0.3))
slow.send_raw("ce\r\nUSER a 0 ")
time.sleep(1.0)
print("  after 'ce\\r\\nUSER a 0 ',       received: %r" % slow.collect(quiet=0.3))
slow.send_raw("* :A\r\n")
time.sleep(0.5)
print("  after '* :A\\r\\n',              received: %s" % summ(slow.collect()))
th.join()
print("  other clients stayed responsive during the %.1fs: %s" % (time.time() - t0, all(responsive)))
slow.close()

print("\n== 4.1 fragmentation table ==")
# 1 NICK split
s = P.Session(PORT); s.send_raw("PASS pass\r\nNICK fr"); time.sleep(0.3)
s.send_raw("ag1\r\nUSER u 0 * :U\r\n")
print("  1 NICK split across writes  -> %s" % summ(s.collect())); s.close()

# 2 one byte at a time
s = P.Session(PORT)
for ch in "PASS pass\r\nNICK byte1\r\nUSER u 0 * :U\r\n":
    s.send_raw(ch); time.sleep(0.002)
print("  2 one byte at a time        -> %s" % summ(s.collect())); s.close()

# 3 CR and LF split across sends
s = P.Session(PORT)
s.send_raw("PASS pass\r\nNICK crlf1\r\nUSER u 0 * :U\r"); time.sleep(0.3)
s.send_raw("\n")
print("  3 CR/LF split               -> %s" % summ(s.collect())); s.close()

# 4 three commands in one write
s = P.Session(PORT); s.send_raw("PASS pass\r\nNICK trio\r\nUSER u 0 * :U\r\n")
print("  4 three commands, one write -> %s" % summ(s.collect())); s.close()

# 5 PRIVMSG split mid-text
r = P.register(PORT, "recv5"); r.send("JOIN #fr5"); r.collect()
s = P.register(PORT, "send5"); s.send("JOIN #fr5"); s.collect(); r.collect(); r.buf = b""
s.send_raw("PRIVMSG #fr5 :hel"); time.sleep(0.4); s.send_raw("lo\r\n")
print("  5 PRIVMSG split mid-text    -> %s" % (r.collect().strip() or "(nothing)"))
r.close(); s.close()

# 6 600 bytes, no CRLF
s = P.Session(PORT)
s.send_raw("PASS pass\r\nNICK big6\r\nUSER b 0 * :B\r\n")
s.collect(); s.buf = b""
s.send_raw("PRIVMSG #x :" + "A" * 600)
time.sleep(0.5)
s.send_raw("\r\nPING :after\r\n")
print("  6 600 bytes no terminator   -> %s" % summ(s.collect())); s.close()

# 7 only CRLF
s = P.Session(PORT); s.send_raw("PASS pass\r\nNICK only7\r\nUSER u 0 * :U\r\n"); s.collect(); s.buf = b""
s.send_raw("\r\n\r\n\r\n"); time.sleep(0.3); s.send_raw("PING :x\r\n")
print("  7 only CRLF then PING       -> %s" % summ(s.collect())); s.close()

# 8 bare LF
s = P.Session(PORT); s.send_raw("PASS pass\nNICK lf8\nUSER u 0 * :U\nPING :x\n")
print("  8 bare LF throughout        -> %s" % summ(s.collect())); s.close()

# 9 NUL mid-line
s = P.Session(PORT); s.send_raw(b"PASS pass\r\nNICK nul9\r\nUSER u 0 * :U\r\nPING :a\x00b\r\n")
print("  9 NUL mid-line              -> %s" % summ(s.collect())); s.close()

# 10 never terminated, then close
s = P.Session(PORT)
s.send_raw("PASS pass\r\nNICK half10\r\nUSER h 0 * :H\r\nPRIV")
s.collect(); s.close()
print("  10 half a command then close -> closed; server alive: %s" %
      ("yes" if "001" in P.reg_then(PORT, "after10", "PING :x", after_reg=False) else "NO"))

print("\n== 4.2 / 4.3 abrupt client death (RST, mid-command) ==")
watcher = P.register(PORT, "watch")
watcher.send("JOIN #dead"); watcher.collect(); watcher.buf = b""

victim = socket.create_connection(("127.0.0.1", PORT))
victim.sendall(b"PASS pass\r\nNICK dead\r\nUSER d 0 * :D\r\nJOIN #dead\r\n")
time.sleep(0.5)
watcher.collect(); watcher.buf = b""
# half a command, then a hard RST (SO_LINGER 0 == kill -9 on the wire)
victim.sendall(b"PRIVMSG #dead :incomp")
victim.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, __import__("struct").pack("ii", 1, 0))
victim.close()
time.sleep(0.8)
print("  watcher saw: %s" % (watcher.collect().strip() or "(nothing)"))
print("  partial line delivered? %s" % ("YES - BUG" if "incomp" in watcher.text() else "no (correct)"))
t = P.reg_then(PORT, "afterkill", "JOIN #dead", "PRIVMSG #dead :server survived")
print("  new client after the kill: %s" % summ(t))
watcher.close()

```


= 4.1 partial command, dribbled out, while another client stays live ==
  after 'NICK ali' with no CRLF, received: ''
  after 'ce\r\nUSER a 0 ',       received: ''
  after '* :A\r\n',              received: 001,002,003,004,005,422
  other clients stayed responsive during the 3.9s: True

== 4.1 fragmentation table ==
  1 NICK split across writes  -> 001,002,003,004,005,422
  2 one byte at a time        -> 001,002,003,004,005,422
  3 CR/LF split               -> 001,002,003,004,005,422
  4 three commands, one write -> 001,002,003,004,005,422
  5 PRIVMSG split mid-text    -> :send5!send5@127.0.0.1 PRIVMSG #fr5 :hello
  6 600 bytes no terminator   -> 403,PONG
  7 only CRLF then PING       -> PONG
  8 bare LF throughout        -> 001,002,003,004,005,422,PONG
  9 NUL mid-line              -> 001,002,003,004,005,422,PONG
  10 half a command then close -> closed; server alive: yes

== 4.2 / 4.3 abrupt client death (RST, mid-command) ==
  watcher saw: :dead!d@127.0.0.1 QUIT :Connection error
  partial line delivered? no (correct)
  new client after the kill: JOIN,331,353,366,324,329
exit=0
--- exits ---
Shell cwd was reset to /home/dlesieur/Documents/ft_irc
(timeout 10m)
