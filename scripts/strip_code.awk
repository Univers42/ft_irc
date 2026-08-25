# ---------------------------------------------------------------------------
# strip_code.awk — blank out everything that is not code, keeping line numbers.
#
# Removes block comments, line comments, string literals and character
# literals, printing one output line per input line so `grep -n` still reports
# the real line number.
#
# Why this exists: the compliance scanners grep the sources for forbidden
# calls and for the single event-wait call site. Prose is not code, and a
# scanner that cannot tell the difference reports a violation for a sentence:
#
#     ** epoll_wait() does two things at once, and both matter here:
#
# That is a comment, not a call site. scripts/check_event_loop.py already
# solved this with its own strip_noise(); this is the shell-side equivalent,
# shared by tests/12_build_norm.sh and scripts/audit.sh so the two agree.
#
# Character literals are handled, not skipped: AbnfLineReader.cpp contains
# `out[i] == '"'`, and a stripper that only knew about double quotes would
# take that quote as the START of a string and desynchronise for the rest of
# the file.
#
# Usage:  awk -f scripts/strip_code.awk FILE
# ---------------------------------------------------------------------------
{
    line = $0
    out  = ""
    i    = 1
    n    = length(line)

    while (i <= n) {
        two = substr(line, i, 2)
        c   = substr(line, i, 1)

        if (inblock) {                       # inside /* ... */, possibly for
            if (two == "*/") {               # several lines
                inblock = 0
                i += 2
            } else {
                i++
            }
            continue
        }

        if (two == "/*") { inblock = 1; i += 2; continue }
        if (two == "//") { break }           # rest of the line is a comment

        if (c == "\"" || c == "'") {         # a literal: skip to its closer,
            q = c                            # honouring backslash escapes
            i++
            while (i <= n) {
                d = substr(line, i, 1)
                if (d == "\\") { i += 2; continue }
                if (d == q)    { i++; break }
                i++
            }
            continue
        }

        out = out c
        i++
    }
    print out
}
