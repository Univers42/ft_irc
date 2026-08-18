#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, PASSWORD, is_alive

r = Report("09: malformed input / pre-registration")

# --- commands before completing registration -------------------------------------------------------
preauth_cmds = [
    "JOIN #x",
    "PRIVMSG someone :hi",
    "PART #x",
    "TOPIC #x",
    "MODE #x +i",
    "KICK #x someone",
    "INVITE someone #x",
]
for cmd in preauth_cmds:
    c = IRCClient(name="preauth").connect()
    c.send(cmd)
    got_error = c.expect(r"451|not registered", timeout=1.0)
    r.check(got_error, f"'{cmd.split()[0]}' before registration is rejected (451), not silently executed")
    c.close()

r.check(is_alive(), "server still alive after all pre-registration probes")

# --- unknown / malformed commands from a registered client -------------------------------------------------------
c = IRCClient(name="malformed").connect()
c.send(f"PASS {PASSWORD}")
c.send("NICK malformed")
c.send("USER u 0 * :U")
c.expect(r"001|Welcome", timeout=2.0)
c.clear()

c.send("FOOBARBAZ some args")
r.check(c.expect(r"421|Unknown command", timeout=1.5), "totally unknown command gets 421 (or equivalent), not silence-and-crash")

c.clear()
c.send("JOIN")  # missing required param
r.check(c.expect(r"461|need more param", timeout=1.5), "JOIN with no channel argument errors cleanly")

c.clear()
c.send("KICK")  # missing all params
c.read_available(timeout=1.0)
r.check("ERROR" not in c.buf.upper() or "461" in c.buf, "bare KICK with no arguments doesn't crash the server")

c.clear()
c.send("")  # literally empty line
c.read_available(timeout=0.5)
r.check(is_alive(), "server survives receiving a fully empty line")

r.check(is_alive(), "server responsive and alive after the full malformed-input barrage")
c.close()

sys.exit(0 if r.summary() else 1)
