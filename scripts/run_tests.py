#!/usr/bin/env python3
"""Run every ft_irc suite, in parallel where that is safe, behind a live matrix.

WHY A SCRIPT RATHER THAN `make -j`
----------------------------------
Two of the suites rebuild the project. audit.sh builds all three tiers, and
tests/12_build_norm.sh runs `make re` -- which is `fclean` first, so for a
moment build/bin/ircserv does not exist. Any suite that happened to be driving
that binary at the time dies with a confusing error that has nothing to do with
what it was testing. `make -j` has no way to express who owns the build tree,
so the schedule lives here instead:

    phase 1  BUILD     serial, produces the artefacts phase 2 runs against
    phase 2  PARALLEL  everything else, against a STAGED COPY of those
                       artefacts

Two mechanisms keep phase 2 safe, and they solve different halves:

  STAGING     covers suites that only READ the binaries. Each is handed
              BIN=<rundir>/bin/ircserv, a snapshot taken after phase 1, so a
              rebuild elsewhere cannot pull the ground out from under it.

  PRIVATE     covers the two suites that REBUILD. Staging cannot help them --
  BUILDDIR    they need a tree of their own to fclean. Both Makefiles take
              BUILDDIR from the environment, so build-norm and audit each get
              a scratch tree and can `make re` as violently as they like
              without build/ ever noticing.

Private BUILDDIRs are what let the two slowest gates leave the critical path.
They used to be serial phase-1 steps purely because they fought over build/;
that cost their full runtime up front, before anything could fan out.

PORTS
-----
Every parallel suite gets its own port block (see SUITES). Nothing is left on
a default, because two suites on 6667 fail in a way that reads like a server
bug rather than a scheduling mistake.

OUTPUT
------
A TTY gets a matrix that redraws in place. Anything else -- a pipe, CI -- gets
plain one-line-per-transition output, because redraw escapes in a log file are
noise. Full output for every suite is captured to <rundir>/<suite>.log, and the
tail of any failure is replayed at the end.

    scripts/run_tests.py                 everything
    scripts/run_tests.py --quick         skip the slow ones (sim, mem)
    scripts/run_tests.py --only unit,grammar
    scripts/run_tests.py --jobs 4
    scripts/run_tests.py --list
"""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

# ── status vocabulary ──────────────────────────────────────────────────────
PENDING = "pending"
RUNNING = "running"
PASSED = "passed"
FAILED = "failed"
SKIPPED = "skipped"

# Emoji plus an ASCII twin: a terminal that cannot render the emoji still has
# to produce an aligned, readable table.
MARK = {
    PENDING: ("⏳", "..", "\033[2m"),      # hourglass  - waiting its turn
    RUNNING: ("\U0001f504", ">>", "\033[36m"),  # arrows     - in flight
    PASSED:  ("✅", "OK", "\033[32m"),      # check      - green
    FAILED:  ("❌", "!!", "\033[31m"),      # cross      - red
    SKIPPED: ("⏭️", "--", "\033[2m"),  # skip       - not applicable
}

RESET = "\033[0m"
DIM = "\033[2m"
BOLD = "\033[1m"


def disable_colour():
    """Strip every escape from our OWN output.

    The child suites are told via NO_COLOR in their environment; this is the
    other half. A log file full of redraw and colour escapes is unreadable,
    and CI captures stdout to exactly that.
    """
    global RESET, DIM, BOLD, MARK
    RESET = DIM = BOLD = ""
    MARK = dict((k, (v[0], v[1], "")) for k, v in MARK.items())


class Suite(object):
    """One runnable suite: how to start it, and where its state lives."""

    def __init__(self, name, phase, argv, env=None, note="", heavy=False):
        self.name = name
        self.phase = phase        # "build" (serial) or "parallel"
        self.argv = argv
        self.env = env or {}
        self.note = note          # one-line description, shown in the matrix
        self.heavy = heavy        # excluded by --quick

        self.status = PENDING
        self.rc = None
        self.started = None
        self.ended = None
        self.proc = None
        self.logpath = None
        self.logfh = None

    @property
    def elapsed(self):
        if self.started is None:
            return 0.0
        return (self.ended or time.time()) - self.started

    def detail(self):
        """The rightmost column: what this suite is doing, or how it ended."""
        if self.status == FAILED:
            return "exit %s  %s" % (self.rc, os.path.basename(self.logpath or ""))
        if self.status == SKIPPED:
            return self.note
        return self.note


# ── the schedule ───────────────────────────────────────────────────────────
# Ports are assigned here, once, and never defaulted. Each block is four wide
# so a suite that needs a second or third port has room without reaching into
# its neighbour's.
def build_suites():
    return [
        # ---- phase 1: serial, produces what phase 2 stages ---------------
        # Only these two. Everything else either reads a staged copy or
        # builds into a tree of its own, so nothing else needs to be here.
        Suite("build", "build",
              ["make", "--no-print-directory", "all"],
              note="compile the full tier"),
        Suite("build-tests", "build",
              ["make", "--no-print-directory", "-C", "tests", "build"],
              note="compile the Google Test runner (C++17)"),

        # ---- phase 2: rebuilds, each in a private BUILDDIR ----------------
        # These two run `make re`. {SCRATCH} expands to a per-suite scratch
        # tree at launch, which is what lets them fclean freely while the
        # read-only suites keep using build/ and the staged snapshot.
        Suite("build-norm", "parallel",
              ["bash", "tests/12_build_norm.sh"],
              env={"IRC_PORT": "6841", "STARTUP_PORT": "6842",
                   "BUILDDIR": "{SCRATCH}",
                   "BIN": "{SCRATCH}/bin/ircserv"},
              note="make targets, C++98, forbidden calls (private tree)"),
        Suite("audit", "parallel",
              ["make", "--no-print-directory", "audit"],
              env={"BUILDDIR": "{SCRATCH}"},
              note="subject compliance, all three tiers (private tree)"),

        # ---- phase 2: parallel, reads a staged binary --------------------
        # Runs the STAGED test_runner, not build/bin's, for the same reason
        # every other parallel suite does: nothing in phase 2 may depend on
        # the live build tree.
        Suite("unit", "parallel",
              ["{STAGED}/test_runner"],
              note="Google Test, in-process"),
        Suite("shell", "parallel",
              ["bash", "tests/run_all.sh", "--skip-build"],
              env={"IRC_PORT": "6801", "STARTUP_PORT": "6802",
                   "GRAMMAR_PORT": "6803", "FUZZ_PORT": "6804"},
              note="black-box suite, live server"),
        Suite("grammar", "parallel",
              ["make", "--no-print-directory", "test-grammar",
               "GRAMMAR_PORT=6811", "FUZZ_PORT=6812"],
              note="RFC 2812 conformance + fuzz"),
        Suite("norm", "parallel",
              ["make", "--no-print-directory", "norm"],
              note="clang-format, tidy, cpplint, cppcheck"),
        Suite("evloop", "parallel",
              ["make", "--no-print-directory", "evloop-run"],
              note="one event wait, straced live"),
        Suite("headers", "parallel",
              ["make", "--no-print-directory", "headers"],
              note="every include names a tracked file"),
        Suite("cycles", "parallel",
              ["python3", "scripts/check_header_cycles.py", "include", "src"],
              note="header include graph is acyclic"),
        Suite("whitespace", "parallel",
              ["make", "--no-print-directory", "whitespace"],
              note="trailing whitespace, final newline"),
        Suite("sim", "parallel",
              ["make", "--no-print-directory", "test-sim", "SIM_PORT=6821"],
              note="populated simulation + 3 probes", heavy=True),
        Suite("mem", "parallel",
              ["make", "--no-print-directory", "test-mem", "MEM_PORT=6831"],
              note="valgrind, scripted clients", heavy=True),
    ]


# ── rendering ──────────────────────────────────────────────────────────────
def term_width(default=100):
    try:
        return max(60, shutil.get_terminal_size((default, 24)).columns)
    except Exception:
        return default


def clip(text, width):
    if width <= 0:
        return ""
    if len(text) <= width:
        return text
    if width <= 1:
        return text[:width]
    return text[:width - 1] + "…"


class Matrix(object):
    """The live table. Redraws in place on a TTY, streams lines otherwise."""

    def __init__(self, suites, stream, tty, ascii_only):
        self.suites = suites
        self.stream = stream
        self.tty = tty
        self.ascii_only = ascii_only
        self.drawn = 0          # lines currently occupied by the table
        self.started = time.time()
        self.last_plain = {}

    def mark(self, status):
        emoji, plain, colour = MARK[status]
        if self.ascii_only:
            return colour + plain + RESET, len(plain)
        # Most terminals render these emoji double-width; pad to a stable 2.
        return emoji, 2

    def render_rows(self, width):
        rows = []
        namew = max(len(s.name) for s in self.suites)
        # columns: mark(2) sp name sp status(8) sp time(7) sp detail
        fixed = 2 + 1 + namew + 1 + 8 + 1 + 7 + 2
        detailw = max(10, width - fixed)

        for s in self.suites:
            glyph, _ = self.mark(s.status)
            _, _, colour = MARK[s.status]
            secs = "%5.1fs" % s.elapsed if s.started else "     -"
            # Only wrap the name when there is something to wrap: emitting a
            # bare RESET after every name is invisible on a terminal but shows
            # up as literal escapes anywhere the output is captured.
            name = ("%s%-*s%s" % (BOLD, namew, s.name, RESET)) if (
                BOLD and s.status == RUNNING) else ("%-*s" % (namew, s.name))
            rows.append("  %s %s %s%-8s%s %s%6s%s  %s%s%s" % (
                glyph, name,
                colour, s.status, RESET if colour else "",
                DIM, secs, RESET if DIM else "",
                DIM, clip(s.detail(), detailw), RESET if DIM else "",
            ))
        return rows

    def draw(self, final=False):
        if not self.tty:
            return
        width = term_width()
        rows = self.render_rows(width)

        done = sum(1 for s in self.suites if s.status in (PASSED, FAILED, SKIPPED))
        failed = sum(1 for s in self.suites if s.status == FAILED)
        running = sum(1 for s in self.suites if s.status == RUNNING)

        header = "%s%sft_irc test matrix%s  %s%d/%d complete  %d running  %d failed  %.0fs%s" % (
            BOLD, "", RESET, DIM, done, len(self.suites), running, failed,
            time.time() - self.started, RESET)

        block = [clip(header, width), ""] + rows + [""]

        out = []
        if self.drawn:
            out.append("\033[%dA" % self.drawn)   # cursor up to the table top
        for line in block:
            out.append("\033[2K" + line + "\n")   # clear the line, then write
        self.stream.write("".join(out))
        self.stream.flush()
        self.drawn = len(block)

    def transition(self, suite):
        """Non-TTY fallback: one line per state change, so a log reads sanely."""
        if self.tty:
            return
        if self.last_plain.get(suite.name) == suite.status:
            return
        self.last_plain[suite.name] = suite.status
        _, plain, _ = MARK[suite.status]
        if suite.status == RUNNING:
            self.stream.write("[%s] %s starting\n" % (plain, suite.name))
        elif suite.status in (PASSED, FAILED):
            self.stream.write("[%s] %s %s in %.1fs (exit %s)\n"
                              % (plain, suite.name, suite.status,
                                 suite.elapsed, suite.rc))
        elif suite.status == SKIPPED:
            self.stream.write("[%s] %s skipped\n" % (plain, suite.name))
        self.stream.flush()


# ── running ────────────────────────────────────────────────────────────────
def launch(suite, rundir, root, staged_bin):
    env = os.environ.copy()

    # Point every parallel suite at the snapshot rather than the live build
    # tree, so a rebuild in another process cannot delete the binary mid-run.
    # This goes in FIRST: a suite that names its own BIN (build-norm, which
    # tests a tree it builds itself) has to win over the staged default.
    if staged_bin and suite.phase == "parallel":
        env["BIN"] = staged_bin

    # {SCRATCH} is this suite's private build tree. Only the two rebuilding
    # gates ask for one; giving each its own is what lets them run `make re`
    # concurrently with everything else instead of ahead of it.
    scratch = os.path.join(rundir, "tree", suite.name)
    if any("{SCRATCH}" in v for v in suite.env.values()):
        os.makedirs(scratch)
    for key, value in suite.env.items():
        env[key] = value.replace("{SCRATCH}", scratch)

    env["MAKEFLAGS"] = ""       # never inherit this run's jobserver
    env.setdefault("NO_COLOR", "1")

    # {STAGED} lets a suite name a binary from the snapshot rather than the
    # live build tree.
    stagedir = os.path.join(rundir, "bin")
    argv = [a.replace("{STAGED}", stagedir) for a in suite.argv]

    suite.logpath = os.path.join(rundir, "%s.log" % suite.name)
    suite.logfh = open(suite.logpath, "wb")
    suite.started = time.time()
    suite.status = RUNNING
    suite.proc = subprocess.Popen(
        argv, cwd=root, env=env,
        stdout=suite.logfh, stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL, start_new_session=True)


def reap(suite):
    rc = suite.proc.poll()
    if rc is None:
        return False
    suite.rc = rc
    suite.ended = time.time()
    suite.status = PASSED if rc == 0 else FAILED
    if suite.logfh:
        suite.logfh.close()
        suite.logfh = None
    return True


def kill_all(suites):
    for s in suites:
        if s.proc and s.proc.poll() is None:
            try:
                os.killpg(os.getpgid(s.proc.pid), signal.SIGTERM)
            except Exception:
                try:
                    s.proc.terminate()
                except Exception:
                    pass
    deadline = time.time() + 5
    for s in suites:
        if not s.proc:
            continue
        while s.proc.poll() is None and time.time() < deadline:
            time.sleep(0.05)
        if s.proc.poll() is None:
            try:
                os.killpg(os.getpgid(s.proc.pid), signal.SIGKILL)
            except Exception:
                pass


def run_phase(suites, matrix, jobs, rundir, root, staged_bin, serial):
    """Drive one phase to completion. Returns True if every suite passed."""
    queue = list(suites)
    live = []
    limit = 1 if serial else jobs

    while queue or live:
        while queue and len(live) < limit:
            suite = queue.pop(0)
            launch(suite, rundir, root, staged_bin)
            matrix.transition(suite)
            live.append(suite)
            matrix.draw()

        time.sleep(0.2)

        for suite in list(live):
            if reap(suite):
                live.remove(suite)
                matrix.transition(suite)
                # A serial phase is a prerequisite chain: if the build fails,
                # running the gates that depend on it only produces noise.
                if serial and suite.status == FAILED:
                    for pending in queue:
                        pending.status = SKIPPED
                        pending.note = "skipped: %s failed" % suite.name
                        matrix.transition(pending)
                    queue = []
        matrix.draw()

    return all(s.status != FAILED for s in suites)


def acquire_lock(root):
    """Refuse to start if another matrix run already owns this repository.

    Two concurrent runs do not merely contend -- they corrupt each other.
    Both rebuild, and `make re` is `fclean` first, so one run deletes object
    files the other is in the middle of compiling against. The symptom is a
    compiler error naming a .d file that "does not exist", which points at
    nothing and reads like a broken Makefile rather than a second run in
    another terminal. Fail early, and say which PID holds it.

    Returns the held fd. The caller must keep the reference alive: closing it
    releases the lock, and so does process exit, which is what we want.
    """
    import fcntl
    import hashlib

    key = hashlib.sha1(os.path.realpath(root).encode()).hexdigest()[:12]
    path = os.path.join(tempfile.gettempdir(), "ftirc-matrix-%s.lock" % key)
    fd = open(path, "a+")
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except (IOError, OSError):
        fd.seek(0)
        holder = fd.read().strip() or "unknown"
        sys.stderr.write(
            "another matrix run is already active in this repository "
            "(pid %s)\n"
            "  Both runs rebuild, so running them together makes each one\n"
            "  delete the other's objects mid-compile. Wait for it, or kill\n"
            "  it, then start again.\n" % holder)
        return None
    fd.seek(0)
    fd.truncate()
    fd.write("%d\n" % os.getpid())
    fd.flush()
    return fd


def stage_binaries(root, rundir):
    """Snapshot build/bin so phase 2 is immune to a concurrent rebuild."""
    src = os.path.join(root, "build", "bin")
    dst = os.path.join(rundir, "bin")
    if not os.path.isdir(src):
        return None
    os.makedirs(dst, exist_ok=True)
    staged = None
    for name in os.listdir(src):
        s = os.path.join(src, name)
        if not os.path.isfile(s):
            continue
        d = os.path.join(dst, name)
        shutil.copy2(s, d)
        os.chmod(d, 0o755)
        if name == "ircserv":
            staged = d
    return staged


def tail(path, lines=25):
    try:
        with open(path, "rb") as fh:
            data = fh.read().decode("utf-8", "replace").splitlines()
    except OSError:
        return ["(no log)"]
    return data[-lines:] if len(data) > lines else data


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobs", "-j", type=int, default=0,
                    help="parallel suites in phase 2 (default: CPU count, capped at 6)")
    ap.add_argument("--quick", action="store_true",
                    help="skip the slow suites (sim, mem)")
    ap.add_argument("--only", default="",
                    help="comma-separated suite names; the build phase is kept")
    ap.add_argument("--skip", default="",
                    help="comma-separated suite names to drop")
    ap.add_argument("--list", action="store_true", help="list suites and exit")
    ap.add_argument("--ascii", action="store_true",
                    help="ASCII status marks instead of emoji")
    ap.add_argument("--keep-logs", action="store_true",
                    help="keep the run directory instead of naming it and moving on")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    suites = build_suites()

    if args.list:
        for s in suites:
            print("  %-11s %-9s %s%s" % (s.name, s.phase, s.note,
                                         "  [heavy]" if s.heavy else ""))
        return 0

    if args.quick:
        suites = [s for s in suites if not s.heavy]
    if args.only:
        want = set(x.strip() for x in args.only.split(",") if x.strip())
        suites = [s for s in suites if s.name in want or s.phase == "build"]
    if args.skip:
        drop = set(x.strip() for x in args.skip.split(",") if x.strip())
        suites = [s for s in suites if s.name not in drop]
    if not suites:
        print("no suites selected")
        return 2

    jobs = args.jobs or min(6, os.cpu_count() or 2)

    lock = acquire_lock(root)
    if lock is None:
        return 2

    tty = sys.stdout.isatty()
    if not tty or os.environ.get("NO_COLOR"):
        disable_colour()
    rundir = tempfile.mkdtemp(prefix="ftirc_tests_")
    matrix = Matrix(suites, sys.stdout, tty, args.ascii)

    if tty:
        sys.stdout.write("\033[?25l")   # hide the cursor while redrawing
        sys.stdout.flush()

    ok = True
    try:
        matrix.draw()
        build = [s for s in suites if s.phase == "build"]
        par = [s for s in suites if s.phase == "parallel"]

        if build:
            ok = run_phase(build, matrix, jobs, rundir, root, None, serial=True)

        staged = stage_binaries(root, rundir)
        if par:
            if not ok:
                # The build phase failed: the binary under build/bin is either
                # missing or untrustworthy, so running the rest would report
                # failures that are all the same failure.
                for s in par:
                    s.status = SKIPPED
                    s.note = "skipped: build phase failed"
                    matrix.transition(s)
                matrix.draw()
            else:
                ok = run_phase(par, matrix, jobs, rundir, root, staged,
                               serial=False) and ok
    except KeyboardInterrupt:
        kill_all(suites)
        ok = False
        sys.stdout.write("\ninterrupted\n")
    finally:
        if tty:
            sys.stdout.write("\033[?25h")   # cursor back on, always
            sys.stdout.flush()

    matrix.draw(final=True)

    failures = [s for s in suites if s.status == FAILED]
    for s in failures:
        print("\n%s%s--- %s (exit %s) %s%s"
              % (BOLD, MARK[FAILED][2], s.name, s.rc, "-" * 30, RESET))
        for line in tail(s.logpath):
            print("    " + line)

    passed = sum(1 for s in suites if s.status == PASSED)
    skipped = sum(1 for s in suites if s.status == SKIPPED)
    print()
    if failures:
        print("%s%d passed, %d FAILED, %d skipped%s  logs: %s"
              % (MARK[FAILED][2], passed, len(failures), skipped, RESET, rundir))
    else:
        print("%severy suite passed%s  (%d suites, %d skipped)  logs: %s"
              % (MARK[PASSED][2], RESET, passed, skipped, rundir))

    if not failures and not args.keep_logs:
        shutil.rmtree(rundir, ignore_errors=True)

    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
