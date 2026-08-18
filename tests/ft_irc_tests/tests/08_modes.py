#!/usr/bin/env python3
"""The largest single evaluation surface in ft_irc. Assumes first-joiner-is-op."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD

r = Report("08: MODE")

op = IRCClient(name="mop").connect()
u1 = IRCClient(name="mu1").connect()
u2 = IRCClient(name="mu2").connect()
register(op, PASSWORD, "mop")
register(u1, PASSWORD, "mu1")
register(u2, PASSWORD, "mu2")
op.send("JOIN #modetest")
op.expect(r"JOIN", timeout=1.0)
op.clear(); u1.clear(); u2.clear()

# --- +o : grant/revoke operator -------------------------------------------------------
u1.send("JOIN #modetest")
op.expect(r"JOIN.*mu1|mu1", timeout=1.0)
op.clear(); u1.clear()

u1.send("MODE #modetest +o mu1")
r.check(u1.expect(r"482|not.*operator", timeout=1.5), "non-operator can't grant +o to themselves or anyone else")

op.clear(); u1.clear()
op.send("MODE #modetest +o mu1")
r.check(u1.expect(r"\+o|MODE.*mu1", timeout=1.5), "operator CAN grant +o")

u1.clear()
u1.send("MODE #modetest +i")  # the freshly-promoted op should now be able to do operator things
r.check(u1.expect(r"\+i|MODE", timeout=1.5), "newly promoted operator can immediately use operator commands")
u1.send("MODE #modetest -i")
u1.read_available(timeout=0.5)

op.clear()
op.send("MODE #modetest -o mu1")
r.check(op.expect(r"-o|MODE.*mu1", timeout=1.5), "operator status can be revoked with -o")

# --- +i : invite-only -------------------------------------------------------
op.clear()
op.send("MODE #modetest +i")
r.check(op.expect(r"\+i|MODE", timeout=1.5), "+i is settable by an operator")
u2.clear()
u2.send("JOIN #modetest")
r.check(u2.expect(r"473|invite only", timeout=1.5), "JOIN blocked on invite-only channel without an invite")
op.clear()
op.send("MODE #modetest -i")
u2.clear()
u2.send("JOIN #modetest")
r.check(u2.expect(r"JOIN.*#modetest|#modetest", timeout=1.5), "-i lets ordinary JOIN through again")

# --- +k : channel key -------------------------------------------------------
op.clear()
op.send("MODE #modetest +k secretkey")
r.check(op.expect(r"\+k|MODE", timeout=1.5), "+k <password> is settable by an operator")

u2.send("PART #modetest")
u2.clear()
u2.send("JOIN #modetest")
r.check(u2.expect(r"475|Cannot join|bad.*key", timeout=1.5), "JOIN without the key is rejected")
u2.clear()
u2.send("JOIN #modetest wrongkey")
r.check(u2.expect(r"475|Cannot join|bad.*key", timeout=1.5), "JOIN with the WRONG key is rejected")
u2.clear()
u2.send("JOIN #modetest secretkey")
r.check(u2.expect(r"JOIN.*#modetest|#modetest", timeout=1.5), "JOIN with the correct key succeeds")

op.clear()
op.send("MODE #modetest -k")
r.check(op.expect(r"-k|MODE", timeout=1.5), "-k removes the channel key")

# --- +l : user limit -------------------------------------------------------
op.clear()
op.send("MODE #modetest +l 2")
r.check(op.expect(r"\+l|MODE", timeout=1.5), "+l <n> is settable by an operator")
# channel currently has op + u1 + u2 == 3 members already, so the limit is
# already exceeded on purpose: a brand-new client should now be refused.
u3 = IRCClient(name="mu3").connect()
register(u3, PASSWORD, "mu3")
u3.clear()
u3.send("JOIN #modetest")
r.check(u3.expect(r"471|channel is full", timeout=1.5), "JOIN beyond the +l limit is rejected")

op.clear()
op.send("MODE #modetest -l")
u3.clear()
u3.send("JOIN #modetest")
r.check(u3.expect(r"JOIN.*#modetest|#modetest", timeout=1.5), "-l removes the limit and JOIN succeeds")

# --- combined flags in one command -------------------------------------------------------
op.clear()
op.send("MODE #modetest +ikl comboKey 5")
op.read_available(timeout=1.0)
r.check("ERROR" not in op.buf.upper(), "combined 'MODE +ikl <key> <limit>' is accepted without a hard error")
op.send("MODE #modetest -i")
op.send("MODE #modetest -k")
op.send("MODE #modetest -l")
op.read_available(timeout=1.0)

# --- malformed MODE -------------------------------------------------------
op.clear()
op.send("MODE #modetest +k")  # missing required argument
r.check(op.expect(r"461|need more param", timeout=1.5), "+k with no key argument errors instead of crashing/hanging")

op.clear()
op.send("MODE #modetest +l")  # missing required argument
r.check(op.expect(r"461|need more param", timeout=1.5), "+l with no limit argument errors instead of crashing/hanging")

op.clear()
op.send("MODE #modetest +x")  # unknown mode letter
op.read_available(timeout=1.0)
r.check("ERROR" not in op.buf.upper() or "472" in op.buf, "unknown mode letter is handled gracefully (no crash)")

op.close(); u1.close(); u2.close(); u3.close()
sys.exit(0 if r.summary() else 1)
