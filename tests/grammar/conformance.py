#!/usr/bin/env python3
"""Drive the RFC 2812 corpus at a live server, one command production at a time.

Three outcomes, the same three scripts/sim/verify_grammar.sh uses:

    PASS      the server does what RFC 2812 says
    FAIL      it contradicts the RFC in a way that matters
    DIVERGE   the RFC leaves it open, or this server is deliberately
              stricter/looser -- recorded, never fatal

On top of the per-case expectation, every case must hold the invariants that
scripts/sim/fuzz_mode.sh established for MODE, generalised to all 18
productions: the server stays up, stays responsive, and every line it emits
is a well-formed IRC line.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import corpus  # noqa: E402
import ircwire as w  # noqa: E402

HOST = "127.0.0.1"
PASSWORD = "grammarpw"

GREEN, RED, YELLOW, DIM, RESET = (
    "\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m"
)
if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
    GREEN = RED = YELLOW = DIM = RESET = ""


def malformed(line):
    """Why this reply is not a well-formed IRC line, or None."""
    if len(line) + 2 > 512:
        return "%d octets, over the 512 limit" % (len(line) + 2)
    if b"\x00" in line:
        return "contains a NUL"
    if b"\r" in line or b"\n" in line:
        return "contains an embedded CR or LF"
    if not line.strip():
        return "blank line"
    return None


# Commands that can leave the shared channel state changed. Anything else is
# a read, so rebuilding the fixture after it would be pure cost -- and the
# rebuild is by far the most expensive thing this suite does.
MUTATING = ("JOIN", "PART", "KICK", "MODE", "TOPIC", "INVITE", "QUIT", "NICK")


class Fixture(object):
    """probe (op of #probe and #probe2), bob (member), carol (elsewhere)."""

    def __init__(self, port):
        self.port = port
        self.probe = w.register(HOST, port, PASSWORD, "probe")
        self.bob = w.register(HOST, port, PASSWORD, "bob")
        self.carol = w.register(HOST, port, PASSWORD, "carol")
        self.reset()

    def reset(self):
        """Rebuild the shared channel state from empty.

        Emptying it first is what makes this reliable: a channel outlives the
        client that created it, so re-joining one bob is still sitting in
        would hand probe plain membership and every op-gated case after that
        would answer 482. Everyone parts, the channel dies, probe recreates
        it and is its operator again.
        """
        for conn in (self.probe, self.bob, self.carol):
            conn.send_line("PART #probe")
            conn.send_line("PART #probe2")
        for conn in (self.probe, self.bob, self.carol):
            conn.collect(0.05)

        self.probe.send_line("JOIN #probe")
        self.probe.send_line("JOIN #probe2")
        self.probe.collect(0.06)
        self.bob.send_line("JOIN #probe")
        for conn in (self.probe, self.bob, self.carol):
            conn.collect(0.05)

    def live(self):
        """fuzz_mode.sh's I1, on a connection the case did not touch."""
        self.carol.send_line("PING liveness")
        for _ in range(8):
            if "PONG" in w.commands(self.carol.collect(0.15)):
                return True
        return False

    def close(self):
        for conn in (self.probe, self.bob, self.carol):
            conn.close()


def run_case(case, fixture, port):
    """-> (outcome, detail). outcome in PASS / FAIL / DIVERGE."""
    if case.fresh:
        conn = w.Conn(HOST, port)
        if case.state == corpus.REGISTERED:
            conn.send_line("PASS " + PASSWORD)
            conn.send_line("NICK t%d" % (abs(hash(case.line)) % 100000))
            conn.send_line("USER t 0 * :T")
            conn.collect(0.25)
        conn.collect(0.05)
    else:
        conn = fixture.probe
        conn.collect(0.02)

    conn.send_line(case.line)
    replies = conn.collect(0.3)

    try:
        for line in replies:
            why = malformed(line)
            if why:
                return "FAIL", "malformed reply (%s): %r" % (why, line[:80])

        codes = w.numerics(replies)
        cmds = w.commands(replies)

        if case.expect_close:
            conn.send_line("PING after-quit")
            conn.collect(0.2)
            if not conn.closed:
                return "DIVERGE", "connection stayed open after QUIT"

        if case.expect_silence and codes:
            outcome = "FAIL" if case.strict else "DIVERGE"
            return outcome, "expected no numeric, got %s" % ",".join(codes)

        if case.want and case.want not in codes:
            outcome = "FAIL" if case.strict else "DIVERGE"
            got = ",".join(codes + [c for c in cmds if not c.isdigit()]) or "nothing"
            return outcome, "wanted %s, got %s" % (case.want, got)

        for bad in case.forbid:
            if bad in codes:
                outcome = "FAIL" if case.strict else "DIVERGE"
                return outcome, "a valid line drew %s" % bad

        return "PASS", ",".join(codes) or "no numeric"
    finally:
        if case.fresh:
            conn.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=w.default_binary())
    ap.add_argument("--port", type=int, default=7500)
    ap.add_argument("--only", help="run one command's cases, e.g. --only MODE")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every case, not just the interesting ones")
    args = ap.parse_args()

    if not os.access(args.binary, os.X_OK):
        sys.stderr.write("not executable: %s\nbuild first: make all\n" % args.binary)
        return 2

    port = w.free_port(args.port)
    groups = corpus.by_command()
    if args.only:
        groups = [(n, c) for n, c in groups if n.upper() == args.only.upper()]
        if not groups:
            sys.stderr.write("no such command in the corpus: %s\n" % args.only)
            return 2

    tally = {"PASS": 0, "FAIL": 0, "DIVERGE": 0}
    failures, diverged = [], []

    print("RFC 2812 per-command conformance — %s" % args.binary)
    print("%d cases, %d productions, port %d\n"
          % (sum(len(c) for _, c in groups), len(groups), port))

    with w.Server(args.binary, port, PASSWORD) as server:
        fixture = Fixture(port)
        dirty = False
        try:
            for name, cases in groups:
                print("%s%s%s" % (DIM, name, RESET))
                for case in cases:
                    if not server.alive():
                        print("%s  SERVER DIED%s on %r" % (RED, RESET, case.line))
                        failures.append((case, "server died"))
                        tally["FAIL"] += 1
                        return report(tally, failures, diverged)

                    if dirty:
                        fixture.reset()
                    dirty = case.cmd in MUTATING
                    outcome, detail = run_case(case, fixture, port)

                    if outcome == "PASS" and not fixture.live():
                        outcome, detail = "FAIL", "server stopped answering PING"

                    tally[outcome] += 1
                    if outcome == "FAIL":
                        failures.append((case, detail))
                    elif outcome == "DIVERGE":
                        diverged.append((case, detail))

                    if outcome != "PASS" or args.verbose:
                        colour = {"PASS": GREEN, "FAIL": RED, "DIVERGE": YELLOW}[outcome]
                        print("  %s%-8s%s %-44s %s%s%s"
                              % (colour, outcome, RESET, case.line[:44],
                                 DIM, detail, RESET))
                        print("           %s%s / %s%s"
                              % (DIM, case.rule, case.note, RESET))
        finally:
            fixture.close()

    return report(tally, failures, diverged)


def report(tally, failures, diverged):
    total = sum(tally.values())
    print("\n" + "-" * 68)
    print("%d cases: %s%d passed%s, %s%d failed%s, %s%d diverged%s"
          % (total, GREEN, tally["PASS"], RESET, RED, tally["FAIL"], RESET,
             YELLOW, tally["DIVERGE"], RESET))

    if diverged:
        print("\ndivergences (recorded, not failures):")
        for case, detail in diverged:
            print("  %-10s %-40s %s" % (case.cmd, case.line[:40], detail))

    if failures:
        print("\n%sfailures:%s" % (RED, RESET))
        for case, detail in failures:
            print("  %-10s %s" % (case.cmd, case.line[:60]))
            print("             %s — %s" % (case.rule, case.note))
            print("             %s" % detail)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
