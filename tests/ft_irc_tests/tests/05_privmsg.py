#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD

r = Report("05: PRIVMSG")

alice = IRCClient(name="alice").connect()
bob = IRCClient(name="bob").connect()
register(alice, PASSWORD, "palice")
register(bob, PASSWORD, "pbob")
alice.clear(); bob.clear()

# 1. user -> user delivery
alice.send("PRIVMSG pbob :hello there")
r.check(bob.expect(r"hello there", timeout=1.5), "PM from alice reaches bob's socket")

# 2. unknown nick
alice.clear()
alice.send("PRIVMSG nobody-here :hi")
r.check(alice.expect(r"401|No such nick", timeout=1.5), "PRIVMSG to an unknown nick returns an error")

# 3. missing target
alice.clear()
alice.send("PRIVMSG")
r.check(alice.expect(r"411|461|No recipient|need more param", timeout=1.5), "PRIVMSG with no target errors")

# 4. missing message text (target with no trailing :text)
alice.clear()
alice.send("PRIVMSG pbob")
r.check(alice.expect(r"412|461|No text to send", timeout=1.5), "PRIVMSG with a target but no message body errors")

# 5. channel delivery
alice.send("JOIN #pmtest")
bob.send("JOIN #pmtest")
alice.expect(r"JOIN", timeout=1.0)
alice.clear(); bob.clear()
alice.send("PRIVMSG #pmtest :channel hello")
r.check(bob.expect(r"channel hello", timeout=1.5), "channel PRIVMSG reaches the other member")

# 6. channel you're not in
carol = IRCClient(name="carol").connect()
register(carol, PASSWORD, "pcarol")
carol.clear()
carol.send("PRIVMSG #pmtest :sneaky")
r.check(carol.expect(r"404|442|Cannot send|not on that channel", timeout=1.5),
        "PRIVMSG to a channel you haven't joined is rejected")

alice.close(); bob.close(); carol.close()
sys.exit(0 if r.summary() else 1)
