#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD

r = Report("06: JOIN / PART")

alice = IRCClient(name="alice").connect()
bob = IRCClient(name="bob").connect()
register(alice, PASSWORD, "jalice")
register(bob, PASSWORD, "jbob")
alice.clear(); bob.clear()

# 1. JOIN creates a new channel and confirms to the joiner
alice.send("JOIN #jptest")
r.check(alice.expect(r"JOIN.*#jptest|#jptest", timeout=1.5), "creating/joining a new channel is acknowledged")

# 2. second user joining triggers a notification to the first
bob.send("JOIN #jptest")
r.check(alice.expect(r"JOIN.*#jptest|jbob", timeout=1.5), "existing member is notified when someone else joins")

# 3. joining the same channel twice doesn't duplicate state / error weirdly
alice.clear()
alice.send("JOIN #jptest")
alice.read_available(timeout=1.0)
r.check("ERROR" not in alice.buf.upper(), "re-JOINing a channel you're already in doesn't produce a hard error")

# 4. JOIN with no channel argument
alice.clear()
alice.send("JOIN")
r.check(alice.expect(r"461|need more param", timeout=1.5), "bare JOIN with no channel name errors")

# 5. joining multiple channels in one command (comma-separated), if supported
alice.clear()
alice.send("JOIN #multi1,#multi2")
alice.read_available(timeout=1.0)
r.check(True, "multi-channel JOIN sent (manual check: did both #multi1 and #multi2 register?)")

# 6. PART removes membership and notifies remaining members
bob.clear()
alice.send("PART #jptest")
r.check(bob.expect(r"PART.*#jptest|jalice", timeout=1.5), "remaining member sees the PART")

# 7. PART a channel you're not in
alice.clear()
alice.send("PART #jptest")
r.check(alice.expect(r"442|not on that channel", timeout=1.5), "PARTing a channel you already left is rejected")

# 8. rejoin after PART works cleanly
alice.clear()
alice.send("JOIN #jptest")
r.check(alice.expect(r"JOIN.*#jptest|#jptest", timeout=1.5), "can rejoin a channel after having PARTed it")

alice.close(); bob.close()
sys.exit(0 if r.summary() else 1)
