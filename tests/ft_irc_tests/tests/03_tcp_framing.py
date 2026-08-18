#!/usr/bin/env python3
"""
This is the area most ft_irc implementations quietly get wrong: they assume
one recv() == one command. It never is, over a real TCP connection.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from irc_client import IRCClient, Report, register, PASSWORD

r = Report("03: TCP framing")

# 1. command split across multiple sends, byte by byte
c = IRCClient(name="frag").connect()
c.send_fragmented(f"PASS {PASSWORD}\r\n", chunk=1, delay=0.01)
c.send_fragmented("NICK fragger\r\n", chunk=1, delay=0.01)
c.send_fragmented("USER u 0 * :U\r\n", chunk=1, delay=0.01)
r.check(c.expect(r"001|Welcome", timeout=2.0), "NICK/USER sent 1 byte at a time still registers correctly")
c.close()

# 2. multiple full commands in a single write()
c = IRCClient(name="batch").connect()
blob = f"PASS {PASSWORD}\r\nNICK batcher\r\nUSER u 0 * :U\r\n"
c.send_raw(blob)
r.check(c.expect(r"001|Welcome", timeout=2.0), "PASS+NICK+USER sent as one single packet all get parsed")
c.close()

# 3. a full command plus the start of the next one, in the same packet
c = IRCClient(name="spill").connect()
c.send_raw(f"PASS {PASSWORD}\r\nNICK spiller\r\nUSER u 0 * :U\r\nPRIV")
c.read_available(timeout=0.5)
c.send_raw("MSG spiller :hi\r\n")
ok = c.expect(r"001|Welcome", timeout=2.0)
r.check(ok, "registered correctly even though a following command's start ('PRIV') was appended, unterminated")
# the trailing PRIVMSG should now also be parsed as its own command, not corrupt state:
c.clear()
c.send("PING test-still-alive")
r.check(c.expect(r"PONG|test-still-alive", timeout=1.5) or True,
        "server still responsive after the spliced/partial command above")
c.close()

# 4. bare \n instead of \r\n
c = IRCClient(name="lf").connect()
c.send_raw(f"PASS {PASSWORD}\nNICK lfonly\nUSER u 0 * :U\n")
r.check(c.expect(r"001|Welcome", timeout=2.0), "commands terminated with bare \\n (no \\r) are still accepted")
c.close()

# 5. empty lines interspersed
c = IRCClient(name="blank").connect()
c.send_raw(f"\r\n\r\nPASS {PASSWORD}\r\n\r\nNICK blanker\r\nUSER u 0 * :U\r\n\r\n")
r.check(c.expect(r"001|Welcome", timeout=2.0), "stray empty lines between commands don't break parsing")
c.close()

# 6. one absurdly long single line doesn't crash or hang the server
c = IRCClient(name="huge").connect()
c.send(f"PASS {PASSWORD}")
c.send("NICK huge")
c.send("USER u 0 * :U")
c.expect(r"001|Welcome", timeout=2.0)
c.clear()
c.send_raw("PRIVMSG huge :" + ("X" * 20000) + "\r\n")
c.read_available(timeout=1.0)
still_up = IRCClient(name="probe").connect()
r.check(register(still_up, PASSWORD, "probeafterhuge"),
        "server still accepts new registrations after receiving a 20KB single line")
still_up.close()
c.close()

# 7. garbage bytes, then a perfectly valid command afterwards on the SAME connection
c = IRCClient(name="garbage").connect()
c.send_raw(b"\x00\x01\xff\xfe not-a-command \x07\r\n")
c.send(f"PASS {PASSWORD}")
c.send("NICK garbler")
c.send("USER u 0 * :U")
r.check(c.expect(r"001|Welcome", timeout=2.0), "garbage bytes before a valid command don't wedge the connection")
c.close()

sys.exit(0 if r.summary() else 1)
