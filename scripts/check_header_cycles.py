#!/usr/bin/env python3
"""Report cycles in the project's header include graph.

Why this exists rather than the libcpp one it replaces:

    vendor/libcpp/vendor/scripts/check_header_cycles.py scans for files ending
    in '.h'. This project has 35 headers and every one of them is '.hpp', so
    that checker walked the tree, matched nothing, printed "No header files
    found" and exited 0 -- and audit.sh reported a clean "no header cycles"
    on the strength of it. A gate that measures nothing but reports a pass is
    worse than no gate: it is a pass you would have cited in a defence.

Scope: quoted includes only. An #include <...> names a system or third-party
header that cannot participate in a cycle with ours, and resolving it would
mean guessing at the compiler's search path.

A cycle among headers is not automatically a compile error -- include guards
break the recursion -- but it does mean the two headers cannot be understood,
or reasoned about, one at a time, and it is usually a sign that a declaration
belongs in a third place. Treated as a failure so it gets fixed rather than
accumulated.

    usage: check_header_cycles.py [root ...]        (default: include src)
"""

import os
import posixpath
import re
import sys

HEADER_SUFFIXES = (".hpp", ".h", ".tpp", ".ipp")
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', re.M)


def header_files(roots):
    """Every header under `roots`, as repo-relative posix paths."""
    found = []
    for root in roots:
        for base, _dirs, files in os.walk(root):
            for name in files:
                if name.endswith(HEADER_SUFFIXES):
                    found.append(os.path.join(base, name).replace(os.sep, "/"))
    return sorted(found)


def resolve(target, source, roots, known):
    """Map one quoted include to a file we actually scanned.

    Tried in the order a compiler would: relative to the including file first
    (the "" form's defining behaviour), then each -I root. Anything that does
    not land on a header we scanned is a system or vendor header -- not our
    cycle to find -- and is dropped.
    """
    candidates = [posixpath.normpath(posixpath.join(posixpath.dirname(source), target))]
    for root in roots:
        candidates.append(posixpath.normpath(posixpath.join(root, target)))
    for candidate in candidates:
        if candidate in known:
            return candidate
    return None


def build_graph(files, roots):
    known = set(files)
    graph = {}
    for path in files:
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            graph[path] = []
            continue
        edges = []
        for target in INCLUDE_RE.findall(text):
            resolved = resolve(target, path, roots, known)
            if resolved is not None and resolved != path:
                edges.append(resolved)
        graph[path] = edges
    return graph


def find_cycles(graph):
    """Every cycle reachable in the graph, as a list of node lists.

    Iterative DFS with an explicit stack: a deep include chain would otherwise
    risk Python's recursion limit, and a checker that dies on a large tree is
    the same broken-gate problem this file exists to fix.
    """
    cycles = []
    seen_signatures = set()
    visited = set()

    for start in sorted(graph):
        if start in visited:
            continue
        stack = [(start, iter(graph.get(start, ())))]
        on_path = [start]
        in_path = {start}

        while stack:
            node, children = stack[-1]
            advanced = False
            for child in children:
                if child in in_path:
                    cycle = on_path[on_path.index(child):] + [child]
                    signature = tuple(sorted(set(cycle)))
                    if signature not in seen_signatures:
                        seen_signatures.add(signature)
                        cycles.append(cycle)
                    continue
                if child in visited:
                    continue
                stack.append((child, iter(graph.get(child, ()))))
                on_path.append(child)
                in_path.add(child)
                advanced = True
                break
            if not advanced:
                stack.pop()
                visited.add(node)
                in_path.discard(node)
                on_path.pop()
    return cycles


def main():
    roots = sys.argv[1:] or ["include", "src"]
    roots = [r for r in roots if os.path.isdir(r)]
    if not roots:
        print("no such directory to scan: %s" % " ".join(sys.argv[1:] or ["include", "src"]))
        return 1

    files = header_files(roots)
    if not files:
        # Loud, and a failure. This is the exact condition the old checker
        # reported as success.
        print("no header files found under %s -- nothing was checked" % ", ".join(roots))
        return 1

    graph = build_graph(files, roots)
    edges = sum(len(v) for v in graph.values())
    cycles = find_cycles(graph)

    if not cycles:
        print("%d headers, %d internal includes, no cycles" % (len(files), edges))
        return 0

    print("%d headers, %d internal includes, %d cycle(s):" % (len(files), edges, len(cycles)))
    for cycle in cycles:
        print("  " + " -> ".join(cycle))
    return 1


if __name__ == "__main__":
    sys.exit(main())
