"""Minimal IRC wire client for grammar conformance work.

Speaks raw bytes over TCP the way nc does, because that is the only way to
send a line the server's own client code would never produce. Nothing here
knows what a command means -- it frames, sends and collects.
"""

import errno
import os
import re
import signal
import socket
import subprocess
import tempfile
import time

CRLF = b"\r\n"
RECV_BUDGET = 0.35

ANSI = re.compile(r"\x1b\[[0-9;]*m")

SIGNAL_NAMES = dict(
    (int(getattr(signal, n)), n)
    for n in dir(signal)
    if n.startswith("SIG") and not n.startswith("SIG_")
)


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
        # Its output used to go to DEVNULL. When the server then went away
        # mid-run all a report could say was "server exited", which reads like
        # a crash the payload caused -- and the one thing that distinguishes a
        # crash from an outside SIGTERM (the server logs a clean shutdown, and
        # dies by signal rather than by exit code) had already been discarded.
        # Keep both, and let death() say which happened.
        self.log = tempfile.NamedTemporaryFile(
            prefix="ircwire_srv_%d_" % port, suffix=".log", delete=False)
        self.status = None
        self.tail = ""

    def __enter__(self):
        # Refuse a port somebody already owns. Racing for the bind and then
        # noticing is not enough: our child needs a few ms to fail, and the
        # probe below would connect to the incumbent first and call it ours.
        try:
            socket.create_connection(("127.0.0.1", self.port), 0.4).close()
            self._close_log()
            raise RuntimeError(
                "port %d is already serving — another ircserv (or another test "
                "run) owns it; this server would lose the bind" % self.port)
        except (socket.error, OSError):
            pass

        self.proc = subprocess.Popen(
            [self.binary, str(self.port), self.password],
            stdout=self.log,
            stderr=subprocess.STDOUT,
        )
        for _ in range(80):
            # Our own process first. If it lost the bind it exits at once, and
            # the reachability probe below would happily connect to whoever
            # already owns the port -- adopting a foreign server, then killing
            # a PID the OS has since reissued.
            if self.proc.poll() is not None:
                self._latch(self.proc.returncode)
                self._close_log()
                raise RuntimeError("server exited during startup: %s"
                                   % (self.tail or "status %s" % self.status))
            try:
                socket.create_connection(("127.0.0.1", self.port), 0.4).close()
                return self
            except (socket.error, OSError):
                time.sleep(0.05)
        raise RuntimeError("server never became reachable on port %d" % self.port)

    def __exit__(self, *_):
        self.stop()
        return False

    def alive(self):
        if self.proc is None:
            return False
        code = self.proc.poll()
        if code is None:
            return True
        # stop() clears self.proc, so latch the status the first time death is
        # seen -- death() is called after the run, when proc is already gone.
        self._latch(code)
        return False

    def _latch(self, code):
        if self.status is not None:
            return
        self.status = code
        try:
            with open(self.log.name, "rb") as fh:
                text = fh.read().decode("utf-8", "replace")
            lines = [l.strip() for l in ANSI.sub("", text).splitlines() if l.strip()]
            self.tail = lines[-1][:200] if lines else ""
        except (IOError, OSError):
            self.tail = ""

    def death(self):
        """Why the server is gone, in the words a reader needs.

        A negative status is death by signal. SIGTERM/SIGINT there means
        something outside sent it: no input can make the server signal itself,
        so that is an environment problem (a concurrent test run, a stray
        kill), not a defect the payload found.
        """
        if self.status is None:
            return "still running"
        said = (" — last said: %s" % self.tail) if self.tail else ""
        if self.status < 0:
            sig = -self.status
            return "crashed on %s%s" % (SIGNAL_NAMES.get(sig, "signal %d" % sig), said)
        if self.status == 0:
            # The server installs handlers for SIGINT/SIGTERM and shuts down
            # through them, so being signalled looks like a clean exit 0 --
            # never a negative status. Mid-run that is the whole tell: no
            # input can make the server decide to stop, so a 0 here means
            # somebody outside sent it a signal.
            return ("exited 0 — a clean shutdown, which only a SIGINT/SIGTERM "
                    "from outside can trigger; suspect a concurrent test run "
                    "or a stray kill, not the payload%s" % said)
        return "exited %d%s" % (self.status, said)

    def stop(self):
        if self.proc is None:
            self._close_log()
            return
        if self.proc.poll() is not None:
            self._latch(self.proc.returncode)
        try:
            self.proc.terminate()
            self.proc.wait(5)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass
        self.proc = None
        self._close_log()

    def _close_log(self):
        """The tail is already cached, so the file itself need not outlive us."""
        try:
            self.log.close()
        except (IOError, OSError, ValueError):
            pass
        try:
            os.unlink(self.log.name)
        except OSError:
            pass


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
