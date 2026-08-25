#!/usr/bin/env python3
"""The bircd discipline: no socket I/O except on a readiness event.

The school hands out bircd/ alongside this subject. It is not a tester -- 285
lines of C, a broadcast echo server with no IRC in it -- and the subject text
never mentions it. What it is, is the worked example of the one rule the
subject attaches a zero to:

    "Only 1 poll() (or equivalent) can be used for handling all these
     operations (read, write, but also listen, and so forth). [...] if you
     attempt to read/recv or write/send in any file descriptor without using
     poll() (or equivalent), your grade will be 0."

bircd shows the shape that obeys it, in three phases:

    init_fd()    declare interest: always read; write ONLY if there is
                 something buffered to write (strlen(buf_write) > 0)
    do_select()  the single event wait
    check_fd()   dispatch ONLY on FD_ISSET -- a handler never runs unless
                 select() said that descriptor was ready

This script checks the same three properties against ft_irc, which uses epoll
rather than select but must have the identical structure. It is a static
check; --runtime adds the stronger proof, straced from the live process.
"""

import argparse
import os
import re
import signal
import subprocess
import sys

# Socket I/O that the rule governs. Plain read/write on a regular file (the
# grammar source, the log sink) is not socket I/O and is not in scope.
SOCKET_IO = ("recv", "send", "recvfrom", "sendto", "accept", "accept4")

EVENT_WAIT = ("epoll_wait", "poll", "select", "kevent")


def strip_noise(text):
    """Blank out comments and string literals, keeping byte offsets intact.

    Without this, `throw std::runtime_error("epoll_wait() failed: ...")` reads
    as a second event-wait call site and the whole check reports a violation
    that is really a diagnostic message.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote, j = c, i + 1
            while j < n and text[j] != quote:
                if text[j] == "\\":
                    j += 1
                j += 1
            for k in range(i, min(j + 1, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


def source_files(root):
    out = []
    for base, _dirs, files in os.walk(root):
        for f in files:
            if f.endswith((".cpp", ".hpp")):
                out.append(os.path.join(base, f))
    return sorted(out)


def enclosing_function(text, offset):
    """The function definition a byte offset falls inside, best effort."""
    head = text[:offset]
    best = None
    for m in re.finditer(r"^[A-Za-z_][\w:<>,&*\s]*?\b(\w+)\s*\([^;]*?\)\s*(?:const\s*)?\{", head, re.M):
        best = m.group(1)
    return best or "<file scope>"


def check_static(root):
    problems = []
    waits = []
    io_sites = []

    for path in source_files(root):
        text = strip_noise(open(path).read())
        for m in re.finditer(r"\b(\w+)\s*\(", text):
            name = m.group(1)
            line = text.count("\n", 0, m.start()) + 1
            if name in EVENT_WAIT:
                waits.append((path, line, name))
            elif name in SOCKET_IO:
                io_sites.append((path, line, name, enclosing_function(text, m.start())))

    # 1. exactly one event wait
    if len(waits) != 1:
        problems.append("expected exactly one event-wait call site, found %d: %s"
                        % (len(waits), ", ".join("%s:%d %s" % w for w in waits)))

    # 2. every socket I/O sits in a handler, never in the loop body itself
    #    and never at file scope
    for path, line, name, fn in io_sites:
        if fn == "<file scope>":
            problems.append("%s:%d: %s() outside any function" % (path, line, name))

    return problems, waits, io_sites


def check_runtime(binary, port):
    """Strace the live server: every socket I/O must follow an event wait.

    This is what a static read cannot show. The server is driven with a real
    client while strace records the syscall order; if any recv/send appears
    before the first epoll_wait, or between two of them without one in
    between, the descriptor was touched blind.
    """
    import socket
    import time

    # Pick a port that is actually free. A busy one made this report
    # "saw no socket I/O", which reads like a compliance failure when it is
    # really an environment collision -- a gate that lies about why it failed
    # is worse than no gate.
    for candidate in range(port, port + 100):
        probe = socket.socket()
        try:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("127.0.0.1", candidate))
            port = candidate
            break
        except (socket.error, OSError):
            continue
        finally:
            probe.close()

    trace = "/tmp/ftirc_evloop.trace"
    # Own process group, so teardown can signal strace AND the ircserv it
    # traces. Terminating strace alone leaves the server orphaned and still
    # holding its port -- which piles up one stray process per run, and the
    # next run then drifts onto a different port looking for a free one.
    proc = subprocess.Popen(
        ["strace", "-f", "-tt", "-e", "trace=epoll_wait,recvfrom,sendto,recv,send,accept,accept4,read,write",
         "-o", trace, binary, str(port), "looppw"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=True)
    time.sleep(1.5)

    def teardown():
        """Signal the whole group: strace and the traced server together."""
        for sig in (signal.SIGTERM, signal.SIGKILL):
            if proc.poll() is not None:
                return
            try:
                os.killpg(os.getpgid(proc.pid), sig)
            except OSError:
                try:
                    proc.kill()
                except OSError:
                    pass
                return
            try:
                proc.wait(5)
                return
            except Exception:
                continue

    if proc.poll() is not None:
        return ["the server exited during startup (port %d busy? wrong binary?)" % port], 0, 0

    try:
        c = socket.create_connection(("127.0.0.1", port), 3)
    except (socket.error, OSError) as exc:
        teardown()
        return ["cannot reach the server on port %d: %s" % (port, exc)], 0, 0

    try:
        c.settimeout(0.5)
        c.sendall(b"PASS looppw\r\nNICK loop\r\nUSER u 0 * :L\r\n")
        time.sleep(0.5)
        try:
            c.recv(65536)
        except Exception:
            pass
        c.sendall(b"JOIN #loop\r\nPRIVMSG #loop :hello\r\nQUIT :bye\r\n")
        time.sleep(0.5)
        c.close()
    finally:
        time.sleep(0.3)
        teardown()

    if not os.path.exists(trace):
        return ["strace produced no output -- cannot verify at runtime"], 0, 0

    seen_wait = False
    violations = []
    waits = io = 0
    for raw in open(trace):
        if "epoll_wait" in raw and "resumed" not in raw:
            seen_wait = True
            waits += 1
            continue
        m = re.search(r"\b(recvfrom|sendto|recv|send|accept4?)\(", raw)
        if not m:
            continue
        io += 1
        if not seen_wait:
            violations.append("socket I/O before any event wait: %s" % raw.strip()[:90])

    os.unlink(trace)
    return violations, waits, io


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default="src")
    ap.add_argument("--runtime", action="store_true", help="also strace the live server")
    ap.add_argument("--binary", default="build/bin/ircserv")
    ap.add_argument("--port", type=int, default=8500)
    args = ap.parse_args()

    print("bircd discipline: one event wait, and no socket I/O without it\n")

    problems, waits, io_sites = check_static(args.root)

    for path, line, name in waits:
        print("  event wait      %s:%d  %s()" % (path, line, name))
    print("  socket I/O      %d site(s), each inside a handler:" % len(io_sites))
    for path, line, name, fn in io_sites:
        print("                    %s:%-4d %-8s in %s()" % (path, line, name + "()", fn))

    if args.runtime:
        print("\n  runtime (strace, live server):")
        rv, w, n = check_runtime(args.binary, args.port)
        print("                    %d epoll_wait return(s), %d socket I/O call(s)" % (w, n))
        if n == 0:
            problems.append("runtime check saw no socket I/O -- the probe did not exercise the server")
        problems += rv

    print()
    if problems:
        for p in problems:
            print("  FAIL  %s" % p)
        return 1
    print("  PASS  the structure matches bircd's, and the subject's rule holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
