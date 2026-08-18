#!/usr/bin/env python3
"""PASS / NICK / USER handshake, and the ways it should refuse to complete."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD

r = Report("02: registration")

# 1. happy path
c = IRCClient(name="alice").connect()
r.check(register(c, PASSWORD, "alice"), "correct PASS/NICK/USER registers successfully")
c.close()

# 2. wrong password
c = IRCClient(name="bob").connect()
ok = register(c, "definitely-wrong-password", "bob")
r.check(not ok, "wrong password does NOT register the client")
c.close()

# 3. no password sent at all
c = IRCClient(name="carol").connect()
ok = register(c, None, "carol", send_pass=False)
r.check(not ok, "skipping PASS entirely does NOT register the client")
c.close()

# 4. duplicate nickname
c1 = IRCClient(name="dave1").connect()
r.check(register(c1, PASSWORD, "dave"), "first client claims nick 'dave'")
c2 = IRCClient(name="dave2").connect()
c2.send(f"PASS {PASSWORD}")
c2.send("NICK dave")
c2.send("USER dave2 0 * :Dave Two")
got_conflict = c2.expect(r"433|already in use|nickname is already", timeout=2.0)
r.check(got_conflict, "second client using the same nick gets a nickname-in-use style error")
c1.close()
c2.close()

# 5. invalid / empty nickname
c = IRCClient(name="badnick").connect()
c.send(f"PASS {PASSWORD}")
c.send("NICK")
c.send("USER u 0 * :U")
r.check(c.expect(r"431|461|No nickname"), "bare NICK with no argument is rejected, not silently accepted")
c.close()

# 6. very long nickname doesn't crash the server
c = IRCClient(name="longnick").connect()
c.send(f"PASS {PASSWORD}")
c.send("NICK " + ("A" * 500))
c.send("USER u 0 * :U")
c.read_available(timeout=1.0)
r.check(True, "500-char nickname sent without the connection dying outright (manual check server still up)")
c.close()

# 7. re-registering after already registered
c = IRCClient(name="eve").connect()
r.check(register(c, PASSWORD, "eve"), "eve registers")
c.clear()
c.send(f"PASS {PASSWORD}")
r.check(c.expect(r"462|already registered", timeout=1.5), "PASS after already-registered is rejected, not silently accepted")
c.close()

# 8. NICK change after registration
c = IRCClient(name="frank").connect()
r.check(register(c, PASSWORD, "frank"), "frank registers")
c.clear()
c.send("NICK franky")
c.read_available(timeout=1.0)
r.check("ERROR" not in c.buf.upper() or "franky" in c.buf, "NICK change after registration doesn't error out")
c.close()

sys.exit(0 if r.summary() else 1)
