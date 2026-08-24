# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An IRC server (RFC 2812) in **strict C++98**, for the 42 curriculum, with HexChat as the
reference client. Single process, single thread, one `epoll` loop, non-blocking everything.
The subject's constraints are load-bearing and CI enforces them — see *Hard constraints*.

## Commands

```bash
git submodule update --init --recursive   # fresh clone; needs vendor/libcpp + vendor/googletest

make all          # full tier (default target of `all`, NOT of bare `make`)
make bonus        # mandatory + Bot + FILE transfer
make mandatory    # pure RFC kernel — the binary to defend on
make verify-tiers # all three, strictly sequential
make re / clean / fclean
./ircserv <port> <password>
```

**Bare `make` prints the help screen and builds nothing** (`.DEFAULT_GOAL := help`).
Anything scripted must say `make all`. `make help` documents every target and overridable
variable and is the most current reference for the build.

Everything generated lives under `build/` (`build/obj/<tier>/`, `build/bin/`); `./ircserv` is
a symlink onto `build/bin/ircserv`. CI fails on any `.o`/`.d` outside `build/`.

### Tests

```bash
make test                                     # Google Test suite (C++17, in-process)
make -C tests build && ./build/bin/test_runner --gtest_filter=Channel*   # a single case
./build/bin/test_runner --gtest_shuffle       # order-dependence check (CI does this)

cd tests && bash run_all.sh                   # black-box shell suite vs a live ./ircserv
cd tests && bash run_all.sh --only 05         # one numbered script
cd tests && bash run_all.sh --skip-build      # skip 12_build_norm.sh (it runs `make re`)
cd tests && ./run_dual.sh                     # same suite under bash + hellish, diffed

python3 tests/grammar/conformance.py --verbose # per-command RFC 2812 grammar conformance
python3 tests/grammar/fuzz.py --cases 1500     # structure-aware fuzz (seeded; prints the seed)
```

The two suites prove different things: Google Test exercises the classes in process (it links
the **full**-tier TU, so Bot and FileTransferExt are in scope); the shell suite drives a real
server over TCP and can split a command across packets, `kill -9` a client mid-sentence, or
freeze a reader.

### Gates

```bash
make norm            # clang-format + clang-tidy + cpplint + cppcheck over src/ include/
make norm-fix        # clang-format -i (mechanical half only)
bash scripts/audit.sh          # subject compliance: C++98 tokens, forbidden calls, one poll site
bash scripts/memcheck.sh --auto # valgrind; exit 0 clean / 97 leak / 90 setup-unverified
bash scripts/normalize.sh --check # whitespace gate
```

`make norm-fix` output **must be committed**: CI runs it and fails if `git diff` over
`src include` is non-empty. A tool that isn't installed is reported *skipped*, not failed —
so a green local `make norm` may mean nothing was run.

## Hard constraints (CI enforces each)

- `-Wall -Wextra -Werror -std=c++98`, zero warnings, on all three tiers.
- No C++11+ tokens anywhere in the build sources; `scripts/audit.sh` greps for them.
- No `fork`/`exec`/`system`/threads. CI asserts the *live* process has no children and one thread.
- **Exactly one event-wait call site** in `src/` (`epoll_wait`). Adding a second `poll`/`select`
  anywhere fails the audit.
- `fcntl` only ever as `fcntl(fd, F_SETFL, O_NONBLOCK)`.
- No external library on the link line: the libcpp modules are **compiled in as objects**, no
  `.a` is linked. Adding a libcpp module means adding its name to `LIBCPP_CORE_NAMES` /
  `LIBCPP_FULL_NAMES` / `LIBCPP98_NAMES` in the Makefile, after checking it is C++98-clean.
- A second `make all` must be a no-op (no relink).
- `ColumnLimit: 120` in `.clang-format` and `linelength=120` in `CPPLINT.cfg` must stay equal.
- Every class carries the canonical form; unwanted copy ctor / `operator=` are declared private
  and left undefined (see any header in `include/`).

## Architecture

### Tiers are a link-time choice, not `#ifdef`

`mandatory` / `bonus` / `full` compile the *same* kernel sources and differ only in which
extra `.cpp` files link and which `src/tiers/tier_<TIER>.cpp` provides `configureSettings()`
and `registerExtensions(Server&)` (declared in `include/ext/RegisterExtensions.hpp`). There is
no preprocessor tiering anywhere. The full tier's extras are additionally gated at runtime on
`FT_IRC_CONFIG`, so without it the full binary behaves like bonus apart from the console sink.

### The event loop

`Server` owns a `libcpp98::Reactor` (epoll), a `map<int, Client*>`, and a
`map<string, Channel*>`. `Client` wraps a `libcpp98::BufferedSocket` holding the recv line
buffer and the bounded send queue. Reads are framed into whole lines by
`Client::extractMessages()`; writes are queued and flushed on `EPOLLOUT`, with
`updateEpollInterest()` recomputing the mask.

Disconnection is two-phase: `disconnectClient()` marks *pending close* so a queued error reply
still drains, `finalizeDisconnect()` closes; `checkPendingCloseTimeouts()` forces the close
with `SO_LINGER 0` after the deadline. `disconnectClientNow()` is the immediate path (sendq
exceeded). Handlers must never assume the client survived a nested call — `handleClientInput`
re-looks-up the fd after every message.

### Parsing: an ABNF grammar with two interchangeable matchers (`src/grammar/`)

RFC 2812 syntax lives as ABNF *text*, not as hand-written parsing code:

1. `EmbeddedGrammarSource` (a string literal in the binary) or `FileGrammarSource`
   (`FT_IRC_GRAMMAR=<path>`) supplies the text, behind `IGrammarSource`.
2. `GrammarBuilder` — scannerless recursive-descent LL(1) — compiles it into `Abnf::Grammar`,
   a flat AST of `GrammarNode`s with named `$captures`.
3. A matcher behind `Abnf::IMatcher` runs it: `Interpreted::TreeMatcher` (default, backtracking
   tree-walk with a step budget) or `Compiled::ProgramMatcher` (`FT_IRC_MATCHER=compiled`,
   Thompson construction into bytecode run by a Pike VM). `test_matcherdifferential.cpp` holds
   the two to the same behaviour.

`Server::handleMessage()` tries the command's own production first (rule `<name>-cmd`, found
by `firstToken` + `_commandRules`), and falls back to the generic `message` rule. That is the
**two parse paths**: on the fast path `Message::fields` points at the `MatchResult` and named
captures work; on the fallback only positional `params` exist. Handlers must use
`Message::fieldOr(name, index)` / `listOr(...)` so they work either way — never branch on which
path ran. See the comment in `include/Message.hpp`.

Deep background: `wiki/GRAMMAR-ARCHITECTURE.md`, `wiki/THOMPSON-NFA.md`.

### Tables, verified at startup

The codebase prefers one declarative table per concern, plus a startup check that throws if
two tables disagree — so a half-added feature fails at boot, not on the wire.

| Concern | Table | Verified by |
| --- | --- | --- |
| Commands | `Server::kCommands[]` in `src/Server.cpp` (name, handler, `needsRegistration`) | `verifyCommandTable()` — every `<name>-cmd` rule needs a handler and vice versa |
| Numerics | `FT_IRC_REPLIES(X)` X-macro in `include/ReplyList.hpp` → codes + text via `include/Replies.hpp` | `verifyReplyTable()` — three digits, no empty text, no duplicates |
| Channel modes | arity in `ChannelModes::table()` (`include/ChannelModes.hpp`), behaviour in `Server::kChannelModeHandlers[]` (`src/CommandOperator.cpp`) | `verifyChannelModeTable()` — a letter must appear in both |

`Dispatch::find` (`include/Dispatch.hpp`) is the shared name→entry lookup; `Bot` uses it too.

**Adding a command** therefore means: an ABNF `foo-cmd` production in the grammar source, a
`{"FOO", &Server::cmdFoo, <needsRegistration>}` row, and the handler in the matching
`Command*.cpp` (`Registration` / `Channel` / `Messaging` / `Operator` / `Query`). Miss either
half and the server refuses to start.

### Extension seam

`IServerExtension` (`include/ext/IServerExtension.hpp`) is the only way optional features touch
the kernel: hooks `onServerStart`, `onTick`, `onClientRegistered`, `onClientDisconnect`,
`onJoin`, `onPart`, `onCommand` (returns true = handled, else 421), `onPrivmsg`,
`reservesNick`. `Bot` and `FileTransferExt` (a base64 relay that never decodes and never
touches disk) are extensions; the mandatory tier registers none. Extensions may also claim
descriptors via `Server::registerExternalFd()`.

### Policy values

`Limits.hpp` holds the compile-time constants; `Settings.hpp` is the mutable singleton
(`settings()`) seeded from them, overridable at runtime only in the full tier via
`FT_IRC_CONFIG`. Read policy through `settings()`, not `Limits::` directly.

### Logging

`Log` (levels quiet→trace, `FT_IRC_LOG`) with a pluggable `ILogSink`; the full tier installs
`FancyLogSink`. `IrcTrace` renders wire lines with numerics named. Secrets are redacted in one
place — `IrcTrace::secretParamIndex()` — consulted by both the raw-wire and the parsed-message
renderers; anything logging a `Message` or a raw line must go through them.

## Runtime environment variables

| Var | Effect |
| --- | --- |
| `FT_IRC_LOG` | `quiet` `error` `warn` `info` (default) `debug` `trace`, or `0`–`5` |
| `FT_IRC_GRAMMAR` | load the ABNF from a file instead of the embedded copy |
| `FT_IRC_MATCHER` | `compiled` selects the bytecode VM; anything else uses the tree matcher |
| `FT_IRC_CONFIG` | full tier only: INI file overriding `[server]` / `[limits]` settings |

## Conventions

- Commit messages: conventional, lowercase, scoped — `refactor(mode): one table for the
  channel-mode letters, one place for 461`.
- Comments explain *why* a shape was chosen (often naming the alternative that was rejected);
  `//<` marks an inline annotation of a condition with concrete examples. Match that register.
- `vendor/libcpp` is a separate submodule with its own style — never reformat it, and never
  widen `NORM_FILES` into it in a commit.
- Docs live in `wiki/` (`DEFENSE-PLAYBOOK.md` for the eval sheet, `RFC-CONFORMANCE.md`,
  `NETWORKING.md`, `LOGGING.md`, `scenarios/` for per-feature transcripts). `tests/README.md`
  documents the shell suite.
