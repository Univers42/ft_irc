"""Sections 5.3.0 - 5.3.4 — USER matrices."""
import sys, random
sys.path.insert(0, "/tmp/claude-101889/-home-dlesieur-Documents-ft-irc/dea91e4a-64c8-4f4d-afc9-2edab401d878/scratchpad")
import ircprobe as P

PORT = 6667
ctr = [0]


def nick():
    ctr[0] += 1
    return "u%d" % ctr[0]


def probe(usercmd, extra=None):
    lines = ["PASS pass", "NICK %s" % nick(), usercmd]
    if extra:
        lines += extra
    t = P.oneshot(PORT, *lines)
    return P.numerics(t), t


def show(section, rows):
    print("\n== %s ==" % section)
    print("%-4s %-40s %-14s %-26s %s" % ("#", "command", "expect", "got", "verdict"))
    for n, cmd, exp, extra in rows:
        got, t = probe(cmd, extra)
        ok = any(e.strip() in got for e in exp.split("/")) if got else (exp == "silence")
        if exp == "silence":
            ok = not got
        print("%-4s %-40s %-14s %-26s %s" % (n, cmd[:40], exp, ",".join(got) or "(silence)",
                                             "ok" if ok else "*** DIFF ***"))


# 5.3.0 arity and form
show("5.3.0 arity / form", [
    (1,  "USER a 0 * :Real",           "001", None),
    (2,  "USER a 0 * :Real Name Jr.",  "001", None),
    (3,  "USER a 0 * :",               "001", None),
    (4,  "USER a 0 * :a:b:c",          "001", None),
    (5,  "USER a 0 * :!@#$%^&*()",     "001", None),
    (6,  "USER a 0 * Real",            "461", None),
    (7,  "USER a 0 * x :y",            "461", None),
    (8,  "USER u 0 :R :R2",            "461", None),
    (9,  "USER a 0 :R",                "461", None),
    (10, "USER a 0",                   "461", None),
    (10, "USER a",                     "461", None),
    (10, "USER",                       "461", None),
])

# 5.3.1 username
show("5.3.1 username", [
    (1,  "USER alice 0 * :A",          "001", None),
    (2,  "USER a 0 * :A",              "001", None),
    (3,  "USER abcdefghij 0 * :A",     "001", None),
    (4,  "USER abcdefghijKLMN 0 * :A", "001", None),
    (5,  "USER a~b#c 0 * :A",          "001", None),
    (6,  "USER ~alice 0 * :A",         "001", None),
    (61, "USER a:b 0 * :A",            "001", None),
    (62, "USER a!b 0 * :A",            "001", None),
    (7,  "USER a@b 0 * :A",            "461", None),
    (8,  'USER "" 0 * :A',             "001/461", None),
    (9,  "USER a b 0 * :A",            "461", None),
])

# 5.3.2 mode
show("5.3.2 <mode>", [
    (i + 1, "USER u %s * :R" % m, "001", None)
    for i, m in enumerate(["0", "4", "8", "12", "15", "*", "abc", "99999999999999", "-1"])
] + [(11, "USER u :0 * :R", "461", None)])

# 5.3.3 unused
show("5.3.3 <unused>", [
    (i + 1, "USER u 0 %s :R" % u, "001", None)
    for i, u in enumerate(["*", "0", "127.0.0.1", "irc.example.org", "*.*", "anything", "..", "%s%s%n"])
] + [(10, "USER u 0 :* :R", "461", None)])

# 5.3.4 realname
show("5.3.4 <realname>", [
    (1, "USER a 0 * :Alice Liddell",   "001", None),
    (2, "USER a 0 * :A",               "001", None),
    (3, "USER a 0 * :",                "001", None),
    (4, "USER a 0 * :with :colons: inside", "001", None),
    (5, "USER a 0 * :tab\there",       "001", None),
    (6, "USER a 0 * :" + "x" * 400,    "001", None),
    (7, "USER a 0 * Alice",            "461", None),
    (9, "USER a 0 *",                  "461", None),
    (10, "USER a 0 * :%n%n%n",         "001", None),
])

# 5.3.1 row 4 — prove the truncation by reading the prefix back
s = P.Session(PORT)
s.send("PASS pass", "NICK tr", "USER abcdefghijKLMN 0 * :T", "JOIN #p")
t = s.collect()
import re
m = re.search(r":tr!(\S+?)@", t)
print("\n5.3.1 row 4 truncation: prefix user part = %r (expect 'abcdefghij', 10 octets)" % (m.group(1) if m else None))
s.close()

# 5.3.4 row 8 / CRLF injection
t = P.oneshot(PORT, "PASS pass", "NICK inj", "USER u 0 * :A", "JOIN #injected")
print("5.3.4 row 8 CRLF: JOIN #injected ran as its own command ->",
      "JOIN #injected" in t or "353" in t, "| numerics:", P.numerics(t))