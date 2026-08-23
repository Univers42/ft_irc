"""Minimal IRC wire client for grammar conformance work.

Speaks raw bytes over TCP the way nc does, because that is the only way to
send a line the server's own client code would never produce. Nothing here
knows what a command means -- it frames, sends and collects.
"""

import errno
import os
import socket
import subprocess
import time

CRLF = b"\r\n"
RECV_BUDGET = 0.35


class Conn(object):
    """One client connection. Collects replies; never interprets them."""

    def __init__(self, host, port, timeout=3.0):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(0.05)
        self.buf = b""
        self.closed = False

    def send_raw(self, data):
        """Send bytes exactly as given -- no CRLF appended, no encoding."""
        try:
            self.sock.sendall(data)
            return True
        except (socket.error, OSError):
            self.closed = True
            return False

    def send_line(self, line):
        if isinstance(line, str):
            line = line.encode("utf-8", "surrogateescape")
        return self.send_raw(line + CRLF)

    def collect(self, budget=RECV_BUDGET):
        """Drain until the stream goes quiet, or `budget` runs out.

        Returning as soon as a complete line has arrived and the socket has
        gone idle, rather than always burning the whole budget, is what keeps
        a 128-case corpus to seconds instead of minutes: almost every reply
        lands within a millisecond or two of the request.
        """
        deadline = time.time() + budget
        got_any = False
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                if got_any and self.buf.endswith(CRLF):
                    break
                continue
            except (socket.error, OSError) as exc:
                if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                    continue
                self.closed = True
                break
            if not chunk:
                self.closed = True
                break
            self.buf += chunk
            got_any = True

        raw, keep = self.buf, b""
        if not raw.endswith(CRLF):
            cut = raw.rfind(CRLF)
            if cut == -1:
                raw, keep = b"", raw
            else:
                raw, keep = raw[: cut + 2], raw[cut + 2 :]
        self.buf = keep
        return [l for l in raw.split(CRLF) if l]

    def close(self):
        try:
            self.sock.close()
        except (socket.error, OSError):
            pass


class Server(object):
    """A private ircserv on its own port, so cases cannot contaminate suites."""

    def __init__(self, binary, port, password="grammarpw"):
        self.binary, self.port, self.password = binary, port, password
        self.proc = None

    def __enter__(self):
        self.proc = subprocess.Popen(
            [self.binary, str(self.port), self.password],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        for _ in range(80):
            try:
                socket.create_connection(("127.0.0.1", self.port), 0.4).close()
                return self
            except (socket.error, OSError):
                if self.proc.poll() is not None:
                    raise RuntimeError("server exited during startup")
                time.sleep(0.05)
        raise RuntimeError("server never became reachable on port %d" % self.port)

    def __exit__(self, *_):
        self.stop()
        return False

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def stop(self):
        if self.proc is None:
            return
        try:
            self.proc.terminate()
            self.proc.wait(5)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass
        self.proc = None


def numerics(lines):
    """The three-digit numerics in a batch of replies, in order."""
    out = []
    for line in lines:
        text = line.decode("utf-8", "replace") if isinstance(line, bytes) else line
        parts = text.split(" ")
        idx = 1 if text.startswith(":") else 0
        if len(parts) > idx:
            token = parts[idx]
            if len(token) == 3 and token.isdigit():
                out.append(token)
    return out


def commands(lines):
    """The command tokens (JOIN, PRIVMSG, 001 ...) in a batch of replies."""
    out = []
    for line in lines:
        text = line.decode("utf-8", "replace") if isinstance(line, bytes) else line
        parts = text.split(" ")
        idx = 1 if text.startswith(":") else 0
        if len(parts) > idx:
            out.append(parts[idx].upper())
    return out


def register(host, port, password, nick, user=None, realname="R N"):
    """Bring a connection all the way to registered, or raise."""
    conn = Conn(host, port)
    conn.send_line("PASS " + password)
    conn.send_line("NICK " + nick)
    conn.send_line("USER %s 0 * :%s" % (user or nick, realname))
    seen = []
    for _ in range(12):
        seen += conn.collect(0.2)
        if "001" in numerics(seen):
            return conn
    conn.close()
    raise RuntimeError("registration never completed for %s (saw %r)" % (nick, numerics(seen)))


def free_port(preferred):
    """A bindable port, starting at `preferred`. CI runners reuse ports."""
    for candidate in range(preferred, preferred + 200):
        probe = socket.socket()
        try:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("127.0.0.1", candidate))
            return candidate
        except (socket.error, OSError):
            continue
        finally:
            probe.close()
    raise RuntimeError("no free port near %d" % preferred)


def default_binary():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "..", "build", "bin", "ircserv")
