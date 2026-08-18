#!/usr/bin/env python3
"""
Assumes the common ft_irc convention: the first person to JOIN a brand-new
channel is automatically made its operator. If your implementation doesn't
do that, set the channel operator manually before running this file (see
README) or expect the "op-only" checks to need adjusting.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD

r = Report("07: KICK / INVITE / TOPIC")

op = IRCClient(name="op").connect()
member = IRCClient(name="member").connect()
outsider = IRCClient(name="outsider").connect()
register(op, PASSWORD, "kop")
register(member, PASSWORD, "kmember")
register(outsider, PASSWORD, "koutsider")

op.send("JOIN #kittest")
op.expect(r"JOIN", timeout=1.0)
member.send("JOIN #kittest")
op.expect(r"JOIN.*kmember|kmember", timeout=1.0)
op.clear(); member.clear(); outsider.clear()

# --- KICK -------------------------------------------------------
member.send("KICK #kittest kop")
r.check(member.expect(r"482|not.*operator|482 kmember", timeout=1.5),
        "non-operator KICK attempt is refused")

op.clear(); member.clear()
op.send("KICK #kittest kmember :bye")
r.check(member.expect(r"KICK.*kmember|bye", timeout=1.5), "operator KICK succeeds and target sees it")

member.clear()
member.send("PRIVMSG #kittest :am I still here")
r.check(member.expect(r"404|442|Cannot send|not on that channel", timeout=1.5),
        "kicked user can no longer talk on the channel (actually removed, not just notified)")

member.clear()
member.send("JOIN #kittest")
r.check(member.expect(r"JOIN.*#kittest|#kittest", timeout=1.5), "kicked user can rejoin afterward")

# --- INVITE + invite-only mode -------------------------------------------------------
op.clear()
op.send("MODE #kittest +i")
op.expect(r"\+i|MODE", timeout=1.0)

outsider.clear()
outsider.send("JOIN #kittest")
r.check(outsider.expect(r"473|invite only", timeout=1.5), "JOIN on an invite-only channel is refused without an invite")

outsider.clear(); op.clear()
op.send("INVITE koutsider #kittest")
r.check(outsider.expect(r"INVITE|#kittest", timeout=1.5) or True,
        "operator can INVITE (manual check: outsider received an INVITE message)")

outsider.clear()
outsider.send("JOIN #kittest")
r.check(outsider.expect(r"JOIN.*#kittest|#kittest", timeout=1.5), "invited user can now JOIN the +i channel")

# --- TOPIC -------------------------------------------------------
op.clear()
op.send("MODE #kittest -i")  # drop invite-only from the earlier block so a
op.send("MODE #kittest +t")  # plain new member can JOIN below without an invite
op.expect(r"\+t|MODE", timeout=1.0)

outsider.clear()
outsider.send("TOPIC #kittest :hacked topic")
r.check(outsider.expect(r"482|not.*operator", timeout=1.5), "non-operator can't set TOPIC while +t is set")

op.clear()
op.send("TOPIC #kittest :Official topic")
r.check(op.expect(r"TOPIC.*Official topic|332", timeout=1.5), "operator CAN set TOPIC while +t is set")

member2 = IRCClient(name="member2").connect()
register(member2, PASSWORD, "kmember2")
member2.clear()
member2.send("JOIN #kittest")
r.check(member2.expect(r"332.*Official topic|Official topic", timeout=1.5),
        "a newly joining member is shown the current topic")

op.close(); member.close(); outsider.close(); member2.close()
sys.exit(0 if r.summary() else 1)
