# err_log.md — defects found while executing the Defense Playbook

Every item below was reproduced on this machine on **2026-08-24** against
commit `13ea711`, full tier, `c++ (GCC) -Wall -Wextra -Werror -std=c++98`.
Each entry gives the command, the observed output, the expected output, and
where the fix belongs.

Findings are split into **program** (the shipped server), **build/tooling**
(scripts, submodules, test harnesses) and **documentation** — documentation
errors were *corrected in place* in [`wiki/DEFENSE-PLAYBOOK.md`](wiki/DEFENSE-PLAYBOOK.md)
and are listed here only as a record of what changed.

| # | Severity | Area | Summary |
| --- | --- | --- | --- |
| [1](#1-critical--a-fresh-clone-does-not-build) | **critical** | build | A fresh clone does not build: six required `vendor/libcpp` files are not in the pinned commit |
| [2](#2-high--the-documented-submodule-command-fails) | high | build | `git submodule update --init --recursive` fails — the command §1 tells the grader to run |
| [3](#3-medium--memchecksh---tierfull-builds-nothing) | medium | tooling | `scripts/memcheck.sh --tier=full` (and interactive mode) builds nothing and can measure the wrong binary |
| [4](#4-medium--fuzzpy-breaks-past-1000-cases) | medium | tooling | `tests/grammar/fuzz.py` generates 10-character nicknames past case 1000 → intermittent CI failure |
| [5](#5-low--kick-does-not-accept-the-rfc-2812-list-forms) | low | program | `KICK` accepts only one channel and one user; RFC 2812 allows comma lists (JOIN/PART do) |
| [6](#6-low--names-is-not-implemented) | low | program | `NAMES` returns `421`; `353`/`366` are only ever sent by `JOIN` |
| [7](#7-info--observations-that-are-not-defects) | info | — | Behaviours that look wrong but are correct; recorded so they are not "fixed" by mistake |

---

## 1. CRITICAL — a fresh clone does not build

**The single most important finding.** The 42 evaluation begins with the grader
cloning the repository into an empty directory (playbook §1, and the subject's
"only the work inside your repository will be evaluated"). That clone cannot be
compiled.

### Reproduce

```bash
rm -rf /tmp/eval && mkdir -p /tmp/eval && cd /tmp/eval
git clone git@github.com:Univers42/ft_irc.git .
git submodule update --init vendor/libcpp vendor/googletest   # succeeds
make all
```

### Observed

```
  LIBCPP  vendor/libcpp/src/str/secure.cpp
make[1]: *** No rule to make target 'build/obj/full/libcpp/str/base64.o', needed by 'build/bin/ircserv'.  Stop.
make: *** [Makefile:249: all] Error 2
```

### Cause

`vendor/libcpp` is pinned at `2ce2f0da0cf77d33cef2960a5cad76846325b262`
(`origin/c98-profile`). Six files the ft_irc build requires exist **only as
uncommitted changes in the local `vendor/libcpp` working tree** and are absent
from that commit:

| Missing from the pinned commit | Needed by |
| --- | --- |
| `src/str/base64.cpp` | `LIBCPP_CORE_NAMES` in the Makefile (FILE relay validation) |
| `include/libcpp/str/base64.hpp` | same |
| `c98/src/traffic_stats.cpp` | `LIBCPP98_NAMES` in the Makefile |
| `c98/include/libcpp98/traffic_stats.hpp` | same |
| `c98/include/libcpp98/expiring_registry.hpp` | `include/bonus/FileTransferExt.hpp` |
| `include/libcpp/log/stream.hpp` | `include/Log.hpp` (`libcpp::log::BasicStream`) |

Verify the divergence:

```bash
git -C vendor/libcpp status --short      # base64.cpp, expiring_registry.hpp, … are ?? / M
ls /tmp/eval/vendor/libcpp/src/str/      # case.cpp format.cpp secure.cpp utf8.cpp — no base64.cpp
```

Note this is invisible locally: the working tree builds because those files are
sitting in it uncommitted. `git status` in ft_irc shows only the one-character
hint `m vendor/libcpp`.

### Fix

1. In `vendor/libcpp`: commit the six files (plus the `secure.cpp` /
   `date.cpp` / header modifications the build depends on) and push to
   `origin/c98-profile`.
2. In `ft_irc`: `git add vendor/libcpp && git commit` to move the pointer to
   that new commit.
3. Prove it: clone into an empty directory and run `make verify-tiers` there.
   Do this **before** the defense — it is the first thing the grader does.

---

## 2. HIGH — the documented submodule command fails

### Reproduce

```bash
cd /tmp/eval && git submodule update --init --recursive
```

### Observed

```
fatal: remote error: upload-pack: not our ref 5ba3135f7cbb54cc6cf72c570776a30c9e287396
fatal: Fetched in submodule path 'vendor/libcpp/vendor/scripts', but it did not contain
       5ba3135f7cbb54cc6cf72c570776a30c9e287396. Direct fetching of that commit failed.
fatal: Failed to recurse into submodule path 'vendor/libcpp'
```

`vendor/libcpp` is left **unchecked-out**, so the build then fails for a second,
more confusing reason.

### Cause

`vendor/libcpp` has its own nested submodule `vendor/scripts` pinned at
`5ba3135f`, a commit that was never pushed to `Univers42/scripts`. This is
already known — `.github/workflows/ci.yml` documents it at length and
deliberately initialises submodules **non-recursively** — but `README.md`,
`wiki/README.md` and playbook §1 all still tell the reader to use
`--recursive`.

### Fix

Either repoint libcpp's nested submodule at a pushed commit, or change the
documented command everywhere to the form CI actually uses:

```bash
git submodule update --init vendor/libcpp vendor/googletest
```

Already corrected in `wiki/DEFENSE-PLAYBOOK.md` §1; `README.md` and
`wiki/README.md` still carry the failing command.

---

## 3. MEDIUM — `memcheck.sh --tier=full` builds nothing

### Reproduce

```bash
make fclean
bash scripts/memcheck.sh --tier=full 6667 pass
```

### Cause

`scripts/memcheck.sh:80`:

```bash
full|"")   make ;;
```

The Makefile sets `.DEFAULT_GOAL := help`, so bare `make` prints the help screen
and **builds nothing** — the Makefile says so explicitly:

> Anything automated must therefore say `make all`, never bare `make`.

Proof:

```bash
make fclean && make | head -3      # prints the help screen
ls build/bin/ircserv               # No such file or directory
```

### Consequence

* Interactive mode (`bash scripts/memcheck.sh`, the form playbook §10.1 and
  `README.md` recommend) also routes through `build_tier ""` → `make`. With no
  binary present it `exec valgrind … ./ircserv` and dies; with a **stale**
  binary present it silently measures that one instead. After a
  `memcheck.sh --auto` run the stale binary is the *mandatory* tier, so a
  "full tier" leak check can quietly measure mandatory.
* It cannot report a leak that isn't there, so it is not a false *green* on
  leaks — but it does not measure what it says it measures.

### Fix

```diff
-		full|"")   make ;;
+		full|"")   make all ;;
```

---

## 4. MEDIUM — `fuzz.py` breaks past 1000 cases

### Reproduce

```bash
python3 tests/grammar/fuzz.py --cases 1500
```

(intermittent — needs the victim connection to be killed by a mutation at
case ≥ 1000, which happens on most but not all seeds)

### Observed

```
  1000/1500 cases, 0 finding(s)
Traceback (most recent call last):
  File "tests/grammar/fuzz.py", line 145, in main
    victim = w.register(HOST, port, PASSWORD, "fuzzer%d" % n)
  File "tests/grammar/ircwire.py", line 168, in register
    raise RuntimeError("registration never completed for %s (saw %r)" % (nick, numerics(seen)))
RuntimeError: registration never completed for fuzzer1087 (saw ['432'])
```

### Cause

`tests/grammar/fuzz.py:145` builds the reconnect nickname as `"fuzzer%d" % n`.
At `n >= 1000` that is 10 characters, over `Limits::kNickLen` (9), so the server
answers **432 ERRONEUSNICKNAME** — correctly. The harness pre-dates commit
`1b6ff6d` *"fix(nick): refuse an over-long nickname with 432 instead of
truncating it"*: before that fix the nick was silently truncated to `fuzzer108`
and the harness worked by accident.

**The server is right; the harness is wrong.** CI runs exactly
`python3 tests/grammar/fuzz.py --cases 1500`, so the `grammar` job carries a
latent flake.

### Fix

```diff
-                victim = w.register(HOST, port, PASSWORD, "fuzzer%d" % n)
+                victim = w.register(HOST, port, PASSWORD, ("fz%d" % n)[:9])
```

---

## 5. LOW — `KICK` does not accept the RFC 2812 list forms

RFC 2812 §3.2.8: `KICK <channel>*( "," <channel> ) <user> *( "," <user> )`.

### Reproduce

```bash
# alice is an operator of both #x1 and #x2, bob and carol are members
KICK #x1,#x2 bob :multi      # -> :ft_irc 403 alice #x1,#x2 :No such channel
KICK #x1 bob,carol :multi    # -> :ft_irc 441 alice bob,carol #x1 :They aren't on that channel
KICK #x1 bob :single         # -> works
```

### Cause

`src/CommandOperator.cpp:88-89` takes the first capture whole and never splits
on `,`:

```cpp
const std::string& chanName = msg.fieldOr("kickchans", 0);
const std::string& target   = msg.fieldOr("kickusers", 1);
```

`cmdJoin` and `cmdPart` **do** split (`Message::list`), so `JOIN #a,#b`,
`JOIN #a,#b k1,k2`, `PART #a,#b` and `JOIN 0` all work — the machinery exists
and is used one command away.

### Impact

Not required by the subject ("KICK — Eject a client from the channel"), and no
error is silent: both forms produce a coherent numeric. Worth fixing only for
RFC completeness; the grammar already names the captures `kickchans` /
`kickusers` in the plural, so the intent was there.

---

## 6. LOW — `NAMES` is not implemented

### Reproduce

```bash
NAMES #ops        # -> :ft_irc 421 alice NAMES :Unknown command
```

`Server::kCommands[]` (`src/Server.cpp:466`) has no `NAMES` entry. `353
RPL_NAMREPLY` / `366 RPL_ENDOFNAMES` are emitted only as part of the `JOIN`
reply burst — which is what HexChat populates its user list from, so the
reference client is unaffected.

### Impact

Not subject-mandated. It mattered here only because the playbook told the
grader to run `NAMES #ops` to check the `@` operator prefix (§7) and after
`MODE +o` (§8.8) — both now corrected to use the `JOIN` burst's own `353`.
`scripts/memcheck.sh` already works around the gap with a comment saying so.

If you add it: one row in `kCommands`, one `names-cmd` production in
`EmbeddedGrammarSource.cpp`, reusing `Channel::getNamesChunks()`, which already
exists.

---

## 7. INFO — observations that are not defects

Recorded so nobody "fixes" them later. Each was checked against RFC 2812 and
the subject.

| Observation | Why it is correct |
| --- | --- |
| A second `PASS` before registration is accepted (no 462) | RFC 2812 §3.1.1 only mandates 462 *after* registration completes. `cmdPass` 462s exactly then. Real clients resend `PASS`. |
| `NICK → PASS → USER` registers successfully | The password is verified in `completeRegistration()`, which runs when NICK *and* USER are both set. Any `PASS` arriving before that counts. `NICK → USER → PASS` correctly gives 464. |
| `PASS` with an empty parameter gives 464, not 461 | The parameter is present, so it is not an arity error; it simply cannot match a password the server refuses to start empty. |
| `USER <mode>` is honoured, not ignored | `applyUserModeBitmask` applies RFC 2812 §3.1.3 bits 4 (`+w`) and 8 (`+i`). `MODE <nick>` then reports `221 +iw`. The playbook used to claim the value was discarded — corrected. |
| A 5000-line flood at a frozen client does **not** trip the SendQ guard | The kernel socket buffer auto-tunes past 150 KiB on loopback and absorbs it. The guard is real: with a 2 KiB `SO_RCVBUF` client and 20 000 lines the server logs `SendQ exceeded` and drops the client. |
| `MODE` accepts at most 13 parameters | `mode-cmd = … *13( SPACE $modeparam )`. Past that the production fails, the generic `message` rule takes over (15-parameter cap, the 15th absorbing the tail) and the excess is answered with `441` + `461`. No crash, coherent replies. |
| `PRIVMSG BOB :hi` is relayed with the target spelled `BOB` | The target token is echoed as the sender wrote it; delivery is casemapped. Standard IRC behaviour. |
| The three `Connection refused` lines from `memcheck.sh --auto` | `wait_for_listen` poll-connecting while valgrind boots. Bash prints the diagnostic itself, so the function's `2>/dev/null` does not suppress it. The run is fine. |

---

## Environment note — concurrent sessions in this repository

While this verification was running, **a second Claude Code session was
operating in the same working tree**: it ran `tests/run_all.sh`, executed
`make re`, committed to `main` (HEAD moved from `0196ce7` to `13ea711` mid-run),
and repeatedly ran

```bash
pgrep -x ircserv | xargs -r kill
```

which killed the server under test three times mid-sweep. Every one of those
terminations was the clean shutdown path — the log ends with
`shutting down — server stopped cleanly`, never a signal or `fatal:` — so no
crash is attributable to the program. All results above were then re-taken with
a private copy of the binary (`ircserv.snapshot`, immune to `pgrep -x ircserv`)
on dedicated ports 6767/6868/6869/6969/6970/6971/7601-7604.

**Do not run two agent sessions against this repository at once**; the second
one's `make re` and `kill` will invalidate the first one's measurements.
