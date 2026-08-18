#!/usr/bin/env python3
"""Abrupt disconnects (socket.close(), no QUIT) must not leave stale state."""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD, is_alive

r = Report("04: disconnect cleanup")

# 1. nickname is freed after an abrupt disconnect
c1 = IRCClient(name="ghost").connect()
r.check(register(c1, PASSWORD, "ghost"), "ghost registers")
c1.close()  # hard close, no QUIT
time.sleep(0.3)

c2 = IRCClient(name="ghost2").connect()
reused = register(c2, PASSWORD, "ghost")
r.check(reused, "nickname 'ghost' is reusable shortly after the original holder disconnected")
c2.close()

# 2. channel membership + notification cleanup on disconnect
observer = IRCClient(name="observer").connect()
register(observer, PASSWORD, "observer")
observer.send("JOIN #cleanup")
observer.clear()

leaver = IRCClient(name="leaver").connect()
register(leaver, PASSWORD, "leaver")
leaver.send("JOIN #cleanup")
observer.expect(r"JOIN.*#cleanup|leaver", timeout=1.5)
observer.clear()

leaver.close()  # abrupt again
r.check(observer.expect(r"QUIT|PART|leaver", timeout=2.0),
        "remaining channel member is notified when the other client vanishes")

# 3. a THIRD client can immediately reuse the leaver's nick — proves the
#    server actually tore down the old client's state, not just the socket
newcomer = IRCClient(name="newcomer").connect()
r.check(register(newcomer, PASSWORD, "leaver"), "the freed nickname is claimable by a brand-new client")
newcomer.close()

# 4. disconnecting mid-command (partial write, then close) doesn't wedge the server
mid = IRCClient(name="midway").connect()
mid.send(f"PASS {PASSWORD}")
mid.send_raw("NICK partia")  # deliberately unterminated, no \r\n
mid.close()
time.sleep(0.2)
r.check(is_alive(), "server survives a client disconnecting mid-command (no trailing CRLF ever sent)")

observer.close()

sys.exit(0 if r.summary() else 1)
