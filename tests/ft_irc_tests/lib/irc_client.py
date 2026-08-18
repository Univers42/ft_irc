"""
Tiny IRC test harness for ft_irc.

Deliberately dumb on purpose: no IRC library, just raw sockets, so you can see
exactly what bytes go over the wire and control framing precisely (that's the
whole point when you're hunting partial-recv() bugs).

Every test script does:

    from irc_client import IRCClient, Report, register

and builds its own little scenario out of that.
"""

import os
import re
import socket
import time

HOST = os.environ.get("IRC_HOST", "127.0.0.1")
PORT = int(os.environ.get("IRC_PORT", "6667"))
PASSWORD = os.environ.get("IRC_PASSWORD", "pass")


class IRCClient:
    def __init__(self, host=None, port=None, name="client"):
        self.host = host or HOST
        self.port = port or PORT
        self.name = name
        self.sock = None
        self.buf = ""

    # -- connection -------------------------------------------------------
    def connect(self, timeout=3.0):
        self.sock = socket.create_connection((self.host, self.port), timeout=timeout)
        return self

    def close(self):
        try:
            if self.sock:
                self.sock.close()
        except OSError:
            pass
        self.sock = None

    # -- sending ------------------------------------------------------------
    def send(self, line):
        """Send one well-formed IRC line (CRLF appended if missing)."""
        if not line.endswith("\r\n"):
            line = line + "\r\n"
        self.sock.sendall(line.encode())

    def send_raw(self, data):
        """Send exactly these bytes, no framing added. For malformed-input tests."""
        if isinstance(data, str):
            data = data.encode()
        self.sock.sendall(data)

    def send_fragmented(self, line, chunk=1, delay=0.02):
        """
        Send one line broken into small pieces with a delay between each,
        forcing the server to see it across multiple recv() calls.
        This is the #1 thing that breaks naive ft_irc implementations.
        """
        if not line.endswith("\r\n"):
            line += "\r\n"
        data = line.encode()
        for i in range(0, len(data), chunk):
            self.sock.sendall(data[i : i + chunk])
            time.sleep(delay)

    # -- receiving ------------------------------------------------------------
    def read_available(self, timeout=1.0):
        """Drain whatever the server has sent within `timeout` seconds and
        append it to the running buffer. Returns the full buffer so far."""
        end = time.time() + timeout
        self.sock.settimeout(0.2)
        while time.time() < end:
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                self.buf += chunk.decode(errors="replace")
            except socket.timeout:
                continue
            except OSError:
                break
        return self.buf

    def expect(self, pattern, timeout=2.0):
        """Poll until `pattern` (regex) shows up in the accumulated buffer."""
        end = time.time() + timeout
        while time.time() < end:
            self.read_available(timeout=0.2)
            if re.search(pattern, self.buf):
                return True
        return False

    def expect_not(self, pattern, timeout=1.0):
        """Confirm `pattern` never shows up within `timeout` (e.g. no crash echo)."""
        self.read_available(timeout=timeout)
        return re.search(pattern, self.buf) is None

    def clear(self):
        self.buf = ""


def register(client, password, nick, user="user", realname="Test User", send_pass=True):
    """Standard PASS/NICK/USER handshake. Returns True if a welcome-ish
    reply (numeric 001, or literally 'Welcome') showed up."""
    if send_pass and password is not None:
        client.send(f"PASS {password}")
    client.send(f"NICK {nick}")
    client.send(f"USER {user} 0 * :{realname}")
    return client.expect(r"(^| )001( |:)|Welcome", timeout=2.0)


def is_alive(host=None, port=None, timeout=1.0):
    """Quick check the server is still accepting connections at all."""
    try:
        s = socket.create_connection((host or HOST, port or PORT), timeout=timeout)
        s.close()
        return True
    except OSError:
        return False


class Report:
    """Tiny pass/fail counter with readable console output."""

    def __init__(self, title):
        self.title = title
        self.passed = 0
        self.failed = 0
        print(f"\n=== {title} ===")

    def ok(self, msg):
        self.passed += 1
        print(f"  [PASS] {msg}")

    def fail(self, msg):
        self.failed += 1
        print(f"  [FAIL] {msg}")

    def check(self, cond, msg):
        if cond:
            self.ok(msg)
        else:
            self.fail(msg)
        return cond

    def summary(self):
        total = self.passed + self.failed
        status = "OK" if self.failed == 0 else "FAILURES"
        print(f"--- {self.title}: {self.passed}/{total} passed [{status}] ---")
        return self.failed == 0
