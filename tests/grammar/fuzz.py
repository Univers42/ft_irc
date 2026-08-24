#!/usr/bin/env python3
"""Structure-aware fuzzer over all 18 command productions.

scripts/sim/fuzz_mode.sh does this for MODE, which is the widest parser
surface in the server. This generalises it: every production gets the same
treatment, and the invariants are the ones that hold no matter what bytes
arrive.

A fuzzer cannot assert an exact reply -- it does not know what the mutated
line means. It asserts what must be true regardless:

    I1  liveness      the server answers PING after every case
    I2  well formed   every reply <= 512 octets, no NUL, no embedded CR/LF
    I3  survival      the process is still running
    I4  no smuggling  a line carrying CR/LF must not execute a second command
    I5  no wedging    the connection still accepts input afterwards

Seeded, so a CI failure reproduces exactly: rerun with --seed from the header.
"""

import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import corpus  # noqa: E402
import ircwire as w  # noqa: E402

HOST = "127.0.0.1"
PASSWORD = "grammarpw"

COMMANDS = ["CAP", "PASS", "NICK", "USER", "QUIT", "PING", "PONG", "JOIN",
            "PART", "PRIVMSG", "NOTICE", "KICK", "INVITE", "TOPIC", "MODE",
            "WHO", "WHOIS", "USERHOST"]

CONTROL = [b"\x00", b"\x01", b"\x07", b"\x0b", b"\x0c", b"\x1b", b"\x1f"]
NASTY = ["", ":", "::", " ", "#", ",", ",,", "#,#", "+", "-", "+-", "0",
         "-1", "99999999999999999999", "%s", "{0}", "\\", "*", "?", "@",
         "a" * 300, "#" + "c" * 300, ":" * 40]


def seeds():
    """Valid lines to mutate: the corpus is already a structural map."""
    return [c.line for c in corpus.CASES if c.want is None and not c.expect_close]


def mutate(line, rng):
    """One structure-aware edit. Returns (bytes, label)."""
    tokens = line.split(" ")
    pick = rng.randrange(14)

    if pick == 0 and len(tokens) > 1:
        i = rng.randrange(1, len(tokens))
        return " ".join(tokens[:i] + tokens[i + 1:]).encode(), "drop-param"
    if pick == 1 and len(tokens) > 1:
        i = rng.randrange(1, len(tokens))
        return " ".join(tokens[:i] + [tokens[i]] + tokens[i:]).encode(), "dup-param"
    if pick == 2:
        return line[: rng.randrange(len(line) + 1)].encode(), "truncate"
    if pick == 3 and len(tokens) > 1:
        i = rng.randrange(1, len(tokens))
        tokens[i] = rng.choice(NASTY)
        return " ".join(tokens).encode(), "nasty-param"
    if pick == 4:
        i = rng.randrange(len(tokens))
        tokens.insert(i, rng.choice(NASTY))
        return " ".join(tokens).encode(), "insert-param"
    if pick == 5:
        return (line + " " + " ".join(rng.choice(NASTY)
                for _ in range(rng.randrange(1, 20)))).encode(), "param-flood"
    if pick == 6:
        raw = line.encode()
        i = rng.randrange(len(raw) + 1)
        return raw[:i] + rng.choice(CONTROL) + raw[i:], "control-byte"
    if pick == 7:
        raw = line.encode()
        i = rng.randrange(len(raw) + 1)
        return raw[:i] + bytes([rng.randrange(0x80, 0x100)]) + raw[i:], "high-byte"
    if pick == 8:
        return line.replace(" ", " " * rng.randrange(2, 6)).encode(), "space-run"
    if pick == 9:
        return line.replace(" ", "\t", 1).encode(), "tab-for-space"
    if pick == 10:
        raw = line.encode()
        i = rng.randrange(len(raw) + 1)
        return raw[:i] + b"\r\n" + raw[i:], "crlf-injection"
    if pick == 11:
        i = rng.randrange(len(line) + 1)
        return (line[:i] + ":" + line[i:]).encode(), "stray-colon"
    if pick == 12:
        return ("".join(c.swapcase() if rng.random() < 0.5 else c
                        for c in line)).encode(), "case-flip"
    return (rng.choice(COMMANDS) + " " +
            " ".join(rng.choice(NASTY)
                     for _ in range(rng.randrange(0, 5)))).encode(), "generated"


def malformed(line):
    if len(line) + 2 > 512:
        return "%d octets, over the 512 limit" % (len(line) + 2)
    if b"\x00" in line:
        return "contains a NUL"
    if b"\r" in line or b"\n" in line:
        return "contains an embedded CR or LF"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=w.default_binary())
    ap.add_argument("--port", type=int, default=7600)
    ap.add_argument("--cases", type=int, default=600)
    ap.add_argument("--seed", type=int, default=None)
    args = ap.parse_args()

    if not os.access(args.binary, os.X_OK):
        sys.stderr.write("not executable: %s\nbuild first: make all\n" % args.binary)
        return 2

    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    rng = random.Random(seed)
    port = w.free_port(args.port)

    print("structure-aware fuzz over 18 productions")
    print("%d cases, seed %d, port %d" % (args.cases, seed, port))
    print("reproduce a failure with:  --seed %d --cases %d\n" % (seed, args.cases))

    pool = seeds()
    findings = []
    labels = {}

    with w.Server(args.binary, port, PASSWORD) as server:
        victim = w.register(HOST, port, PASSWORD, "fuzzer")
        watcher = w.register(HOST, port, PASSWORD, "watcher")
        victim.send_line("JOIN #fuzz")
        victim.collect(0.15)

        for n in range(args.cases):
            payload, label = mutate(rng.choice(pool), rng)
            labels[label] = labels.get(label, 0) + 1

            if victim.closed:
                # Must fit NICKLEN. "fuzzer%d" is 10 characters from n=1000 and
                # the server answers 432 -- correctly, since 1b6ff6d stopped
                # truncating over-long nicks. CI runs --cases 1500, so this was
                # a latent flake that only fired when a mutation killed the
                # victim late in the run.
                victim = w.register(HOST, port, PASSWORD, ("fz%d" % n)[:9])
                victim.send_line("JOIN #fuzz")
                victim.collect(0.1)

            victim.send_raw(payload + b"\r\n")
            replies = victim.collect(0.08)

            # I3 survival. Say HOW it died: a crash the payload caused and an
            # outside SIGTERM look identical from here, and only one of them is
            # a bug in the server.
            if not server.alive():
                findings.append((label, payload, "server gone — %s" % server.death()))
                break

            # I2 well formed
            bad = next(((r, malformed(r)) for r in replies if malformed(r)), None)
            if bad:
                findings.append((label, payload,
                                 "malformed reply (%s): %r" % (bad[1], bad[0][:70])))
                continue

            # I4 no smuggling: a CR/LF inside the payload must not run a
            # second command. QUIT is the one that would be visible.
            if b"\r" in payload or b"\n" in payload:
                if victim.closed and b"QUIT" not in payload.upper():
                    findings.append((label, payload,
                                     "embedded CRLF closed the connection"))
                    continue

            # I1 liveness, on a connection the case never touched
            watcher.send_line("PING live%d" % n)
            alive = False
            for _ in range(6):
                if "PONG" in w.commands(watcher.collect(0.12)):
                    alive = True
                    break
            if not alive:
                why = "" if server.alive() else " (%s)" % server.death()
                findings.append((label, payload,
                                 "server stopped answering PING" + why))
                break

            if (n + 1) % 100 == 0:
                print("  %4d/%d cases, %d finding(s)"
                      % (n + 1, args.cases, len(findings)))

        # I5 no wedging: a fresh client must still be able to register
        try:
            after = w.register(HOST, port, PASSWORD, "afterfuzz")
            after.close()
        except Exception as exc:
            # Nearly always a follow-on from an earlier finding: if the server
            # is gone, "connection refused" is the symptom, not the cause.
            why = "" if server.alive() else " — %s" % server.death()
            findings.append(("<teardown>", b"",
                             "cannot register after fuzzing: %s%s" % (exc, why)))

        victim.close()
        watcher.close()

    print("\nmutations exercised: %s"
          % ", ".join("%s=%d" % kv for kv in sorted(labels.items())))

    if findings:
        print("\n%d finding(s):" % len(findings))
        for label, payload, why in findings[:20]:
            print("  [%s] %s" % (label, why))
            print("      payload: %r" % payload[:120])
        print("\nreproduce: --seed %d --cases %d" % (seed, args.cases))
        return 1

    print("\n%d cases, no findings — invariants held throughout" % args.cases)
    return 0


if __name__ == "__main__":
    sys.exit(main())
