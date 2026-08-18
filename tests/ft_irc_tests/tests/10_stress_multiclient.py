#!/usr/bin/env python3
"""
Two things this file is really checking:
  1. The server doesn't fall over with a bunch of clients doing normal things.
  2. One client that never reads its socket can't stall everybody else — a
     dead giveaway that a blocking send() snuck in somewhere.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD, is_alive

r = Report("10: multi-client stress")

# --- 1. plain fan-in: N clients, all register, all join the same channel -------------------------------------------------------
N = 15
clients = []
for i in range(N):
    c = IRCClient(name=f"s{i}").connect()
    ok = register(c, PASSWORD, f"stress{i}")
    clients.append(c)
    if not ok:
        r.fail(f"client {i} failed to register during fan-in")
        break
else:
    r.ok(f"all {N} clients registered")

for c in clients:
    c.send("JOIN #stress")
for c in clients:
    c.expect(r"JOIN.*#stress|#stress", timeout=2.0)
r.check(is_alive(), f"server still alive after {N} clients joined the same channel")

for c in clients:
    c.close()

# --- 2. slow / non-reading client must not block everyone else -------------------------------------------------------
slowpoke = IRCClient(name="slowpoke").connect()
register(slowpoke, PASSWORD, "slowpoke")
slowpoke.send("JOIN #slowtest")
slowpoke.read_available(timeout=0.5)
# From here on we deliberately never call slowpoke.read_available() again —
# its kernel receive buffer will just fill up and stay full.

flooder = IRCClient(name="flooder").connect()
register(flooder, PASSWORD, "flooder")
flooder.send("JOIN #slowtest")
flooder.read_available(timeout=0.5)

probe = IRCClient(name="probe").connect()
register(probe, PASSWORD, "probe")

start = time.time()
big_msg = "X" * 800
FLOOD_COUNT = 300
for i in range(FLOOD_COUNT):
    flooder.send_raw(f"PRIVMSG #slowtest :{big_msg}\r\n")
flooder.send_raw(f"PRIVMSG probe :flood-done-marker\r\n")

got_marker = probe.expect(r"flood-done-marker", timeout=8.0)
elapsed = time.time() - start

r.check(got_marker, "an unrelated client still received a direct message during/after the flood")
r.check(elapsed < 8.0, f"flood + direct message resolved in {elapsed:.2f}s (< 8s budget) — server isn't stalled by the non-reading client")
r.check(is_alive(), "server still accepting new connections while one client's buffer is backed up")

slowpoke.close()
flooder.close()
probe.close()

# --- 3. clients disconnecting mid-activity, survivors keep working -------------------------------------------------------
a = IRCClient(name="surv_a").connect()
b = IRCClient(name="surv_b").connect()
victim = IRCClient(name="surv_v").connect()
register(a, PASSWORD, "surva")
register(b, PASSWORD, "survb")
register(victim, PASSWORD, "survv")
for c in (a, b, victim):
    c.send("JOIN #survive")
    c.read_available(timeout=0.5)

victim.close()  # abrupt mid-session disconnect

a.clear(); b.clear()
a.send("PRIVMSG #survive :still here?")
r.check(b.expect(r"still here", timeout=2.0), "remaining members can still exchange messages right after a peer's abrupt disconnect")

a.close(); b.close()
sys.exit(0 if r.summary() else 1)
