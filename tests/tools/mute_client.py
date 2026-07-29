#!/usr/bin/env python3
"""
T2 — Silent client with a fully open socket.

Purpose: decide whether a registered client that sends nothing is dropped by the
server's idle timeout, without any possibility of "phantom input" from the test tool.

This client performs EXACTLY ONE sendall() and never writes again. The socket stays
fully open (no shutdown, no close) until the server closes it or the run ends.

Usage:
    python3 mute_client.py [host] [port] [password] [nick] [seconds]
Defaults:
    127.0.0.1 6667 test123 mute1 400

What to look for:
  - Timestamped server lines, in particular how many PINGs arrive.
  - Whether "SERVER CLOSED THE CONNECTION" appears, and at what elapsed time.

Interpretation:
  - Closed at ~240s (+/- the 30s sweep)  -> timeout works; T2 was an nc artifact.
  - One PING, never closed               -> server bug: detection fires, teardown
                                            never completes for silent open sockets.
  - Repeated PINGs, never closed         -> something IS resetting the activity
                                            timer (but it is not this client).
"""

import socket
import sys
import time

host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 6667
password = sys.argv[3] if len(sys.argv) > 3 else "test123"
nick = sys.argv[4] if len(sys.argv) > 4 else "mute1"
duration = float(sys.argv[5]) if len(sys.argv) > 5 else 400.0

registration = (
    "PASS {p}\r\n"
    "NICK {n}\r\n"
    "USER {n} 0 * :M\r\n"
).format(p=password, n=nick).encode()


def stamp(elapsed):
    return "{clock}  t+{el:6.1f}s".format(
        clock=time.strftime("%H:%M:%S"), el=elapsed
    )


sock = socket.create_connection((host, port))
start = time.time()

# The one and only write this process will ever perform on this socket.
sock.sendall(registration)
bytes_sent = len(registration)
print("{s}  >>> sent registration ({b} bytes). This client will not write again."
      .format(s=stamp(0.0), b=bytes_sent), flush=True)

sock.settimeout(1.0)
ping_count = 0
closed_at = None

try:
    while True:
        elapsed = time.time() - start
        if elapsed >= duration:
            print("{s}  --- run finished, still connected ---".format(s=stamp(elapsed)),
                  flush=True)
            break
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        except OSError as exc:
            print("{s}  --- socket error: {e} ---".format(s=stamp(elapsed), e=exc),
                  flush=True)
            closed_at = elapsed
            break

        elapsed = time.time() - start
        if not data:
            print("{s}  *** SERVER CLOSED THE CONNECTION ***".format(s=stamp(elapsed)),
                  flush=True)
            closed_at = elapsed
            break

        for line in data.decode("utf-8", "replace").split("\r\n"):
            if not line:
                continue
            print("{s}  {l}".format(s=stamp(elapsed), l=line), flush=True)
            # A server PING starts with "PING" or ":<server> PING"
            parts = line.split()
            if parts and (parts[0].upper() == "PING"
                          or (len(parts) > 1 and parts[1].upper() == "PING")):
                ping_count += 1
finally:
    print("", flush=True)
    print("=== SUMMARY ===", flush=True)
    print("bytes written by this client after connect: {b} (registration only)"
          .format(b=bytes_sent), flush=True)
    print("server PINGs received: {c}".format(c=ping_count), flush=True)
    if closed_at is None:
        print("server closed connection: NO (ran for {d:.0f}s)".format(d=duration),
              flush=True)
    else:
        print("server closed connection: YES, at t+{c:.1f}s".format(c=closed_at),
              flush=True)
    sock.close()
