#!/usr/bin/env python3
"""Every #include "..." in a tracked source must name a tracked file.

Three times on this branch a commit landed an #include whose header was still
untracked (Dispatch.hpp, ReplyList.hpp, Settings.hpp). It builds locally --
the file is sitting in the working tree -- and dies on a fresh clone with a
fatal missing-header error, so CI goes red across every job while the local
tree looks fine. Comparing the include graph against the index catches it
before the push.
"""

import os
import posixpath
import re
import subprocess
import sys

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', re.M)
SOURCE_RE = re.compile(r"\.(cpp|hpp|h)$")

# Roots the build passes as -I, project side only. Anything under a submodule
# is that submodule's problem, not ours.
ROOTS = ["include", "src", "tests", "vendor", "."]
SUBMODULE_PREFIXES = ("libcpp/", "libcpp98/")


def tracked_files():
    out = subprocess.check_output(["git", "ls-files", "-z"])
    return set(p for p in out.decode("utf-8", "replace").split("\0") if p)


def resolves(include, includer, tracked):
    candidates = [posixpath.normpath(posixpath.join(posixpath.dirname(includer), include))]
    candidates += [posixpath.join(root, include) for root in ROOTS]
    return any(posixpath.normpath(c) in tracked for c in candidates)


def main():
    try:
        tracked = tracked_files()
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.stderr.write("cannot read the git index: %s\n" % exc)
        return 2

    sources = [
        f
        for f in tracked
        if SOURCE_RE.search(f) and f.split("/", 1)[0] in ("src", "include", "tests")
    ]

    missing = []
    for path in sorted(sources):
        if not os.path.exists(path):
            continue
        with open(path, "r") as handle:
            body = handle.read()
        for match in INCLUDE_RE.finditer(body):
            include = match.group(1)
            if include.startswith(SUBMODULE_PREFIXES):
                continue
            if resolves(include, path, tracked):
                continue
            line = body.count("\n", 0, match.start()) + 1
            missing.append((path, line, include))

    for path, line, include in missing:
        print('%s:%d: #include "%s" — the header is not tracked' % (path, line, include))

    if missing:
        print("%d untracked header reference(s) in %d sources" % (len(missing), len(sources)))
        return 1
    print("%d sources checked, every include tracked" % len(sources))
    return 0


if __name__ == "__main__":
    sys.exit(main())
