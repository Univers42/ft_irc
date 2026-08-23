# ft_irc — Defense Map: subject, evaluation sheet, and what proves each

A quick reference tying together three things for the defense:
1. **what the subject demands** (obligations, and the traps that sink projects),
2. **what the evaluation sheet adds** (only where it goes beyond the subject),
3. **what we can show** — the test or command that demonstrates it, with a one-line example.

Read it as: *"if the evaluator asks about X, here's what proves it."*

---

## 0. The eliminatory checks (get these wrong → mark is 0)

The sheet is explicit: if any Basic check fails, **the evaluation ends and the mark is 0.**
These four matter more than everything else combined.

### 0.1 Build: Makefile, C++98, `-Wall -Wextra -Werror`, correct binary name
- **Subject / sheet**: compiles clean under the mandated flags; Makefile has the
  required rules; binary is `ircserv`.
- **What proves it**: `bash scripts/audit.sh` — builds all three tiers under
  `c++ -std=c++98 -Wall -Wextra -Werror`, fails on *any* warning, and checks the
  Makefile exposes `all/bonus/mandatory/clean/fclean/re` plus no-relink.
- **Also**: `make verify-tiers` builds mandatory→bonus→full sequentially, clean.
- **Trap**: `audit.sh` treats *any* build stderr as failure — that's why the build
  is serial (`.NOTPARALLEL`), because forcing `-j` in a recursive make emits a
  jobserver warning the audit would count as a failure.

### 0.2 A single `poll()`/epoll/select — and it's called *before every* I/O
- **Subject / sheet**: only **one** poll-equivalent in the whole program; and
  every `accept`/`recv`/`send` must happen **only in response to an epoll event**,
  never speculatively. After I/O, **errno must not drive logic** (no "got EAGAIN,
  read again").
- **What proves the count**: `audit.sh` §"single event-wait" — greps for
  `epoll_wait|poll|select|kevent`, *excluding strings and comments*, and asserts
  exactly one. Ours is `src/Server.cpp:143`, inside `Server::run()`.
- **Trap (important)**: a naive grep counts **two**, because the error message
  string `"epoll_wait() failed: "` (Server.cpp:146) contains the text. That is
  **not a second call** — it's a string literal. Our audit filters it with
  `grep -v '"'`; a teammate's simpler grep does not, hence the false "2".
- **What proves the architecture** (this part is **defended verbally, not by a
  test** — the evaluator reads the loop): our loop does I/O *only* inside the
  `for` over `epoll_wait`'s returned events; `handleClientInput` does one `recv`
  and, on `<= 0`, returns and waits for the next event — it never inspects errno
  to retry. Be ready to walk the evaluator through `Server::run()` showing this.

### 0.3 `fcntl()` only as `fcntl(fd, F_SETFL, O_NONBLOCK)`
- **Subject / sheet**: any other `fcntl` use is forbidden.
- **What proves it**: `audit.sh` §"fcntl usage" — greps real `fcntl(` calls
  (those with an argument list), excludes the ones carrying `F_SETFL` +
  `O_NONBLOCK`, and fails if anything remains. Ours: 5 calls, all compliant
  (`Server.cpp:91,217`, `PlatformBus.cpp:72,118`).
- **Trap**: same string-literal artifact — `"fcntl() failed"` error messages
  contain the text but aren't calls; the audit only counts calls with a comma
  in the argument list.

### 0.4 No forbidden functions / no C++11+
- **Subject / sheet**: no `fork`/`system`/`exec*`/threads; strictly C++98.
- **What proves it**: `audit.sh` §"forbidden functions" and §"C++98 compliance"
  — token scans across all build sources. Zero hits.

---

## 1. Networking (sheet: Yes/No)

### 1.1 Listens on all interfaces, on the CLI port
- **Sheet**: server listens on all network interfaces (`0.0.0.0`) on the given port.
- **What proves it**: code fact — `bind` to `INADDR_ANY` (`Server.cpp`). Integration
  tests connect over loopback; the "all interfaces" part is defended by pointing at
  the `INADDR_ANY` bind. `test_integration.cpp` ⟨verificar suite name⟩ binds+connects.

### 1.2 nc and the reference client (HexChat) can both connect and get answers
- **Sheet**: connect via `nc`, send commands, get replies; then the same with the
  reference client (HexChat).
- **What proves it**: the whole registration path in `test_integration.cpp`
  (`PASS`/`NICK`/`USER` → `001`–`005`). Live: the USER-GUIDE walks the exact nc and
  HexChat sessions. Manual verification is in `tests/MANUAL-TESTING.md`.

### 1.3 Multiple simultaneous connections, server never blocks
- **Sheet**: handle many connections at once; test nc + HexChat together; server
  must answer all, never block.
- **What proves it**: non-blocking design (single epoll loop, `O_NONBLOCK` sockets).
  `test_robustness.cpp` drives multiple concurrent clients. Live: connect HexChat
  and nc simultaneously, message across.
- **Defended verbally**: "never blocks" = every socket is `O_NONBLOCK`, all I/O is
  event-driven; no blocking `recv`/`send` anywhere.

### 1.4 JOIN a channel; messages broadcast to all members
- **Sheet**: join via the proper command; a client's channel messages reach all
  other members.
- **What proves it**: `test_integration.cpp` — `ChannelMessage` / JOIN + PRIVMSG
  broadcast ⟨verificar test names⟩. Live: two clients in `#test`, one messages, the
  other receives.

---

## 2. Networking specials — robustness (sheet: Yes/No)

This is where the sheet stresses the server with "strange situations." It maps
almost one-to-one onto our robustness suite (T6) and the deferred-teardown work (T4).

### 2.1 Partial commands; other connections stay fine
- **Sheet**: send partial commands via nc; server answers correctly; other
  connections keep working with a partial pending.
- **What proves it**: `test_integration.cpp` line-reassembly tests — command split
  across writes, split between `\r` and `\n`, two commands in one write, bare `\n`.
  ⟨verificar test names⟩ Live: verified with `strace` in MANUAL-TESTING §subject test.

### 2.2 Unexpectedly kill a client; server stays operational
- **Sheet**: kill a client abruptly; server still serves others and new clients.
- **What proves it**: `test_robustness.cpp` abrupt-disconnect (`EPOLLHUP`/`recv==0`
  path). Live: Ctrl+C an nc, confirm others unaffected and channel lists cleaned.

### 2.3 Kill an nc mid-command; server not stuck
- **Sheet**: kill an nc with half a command sent; server not in an odd/blocked state.
- **What proves it**: same abrupt-disconnect path plus the line-buffer tests — a
  half-line in the buffer is simply discarded when the socket closes.

### 2.4 ^Z a reader + flood from another; no hang, drains on resume, no leaks
- **Sheet**: the marquee robustness test — freeze a reader, flood the channel from
  another client; server must not hang; on resume, buffered commands process; check
  for memory leaks.
- **What proves it**: **T6** — `test_robustness.cpp`: `ThirdClientUnaffectedByFrozenReaderFlood`,
  `ServerSurvivesFloodAgainstFrozenReader`, `FrozenReaderEventuallyDisconnectedOnSendQ`
  (self-terminating flood, asserts isolation/survival *during* the flood).
  **Leaks**: `scripts/memcheck.sh --auto` drives register/JOIN/PRIVMSG/PART/QUIT +
  abrupt-disconnect under valgrind with a 3-way exit gate.
- **Defended verbally**: backpressure design — a slow/frozen reader's send queue
  fills to `MAX_SENDQ`, then that client is dropped with `SendQ exceeded`; other
  clients are never blocked because output is per-client and event-driven.
- **Trap we cleared**: an earlier "idle client never evicted" report was an `nc`
  harness artifact (nc stays alive on `sleep`), not a server bug — the timeout works
  (PING ~120s, close ~240s), verified with an instrumented mute client.

---

## 3. Client commands — basic (sheet: Yes/No)

### 3.1 Authenticate, set nick, username, join — via nc and HexChat
- **Sheet**: full registration works in both clients.
- **What proves it**: `test_integration.cpp` registration + `test_conformance.cpp`.
  T1 reversed: an over-long nick now draws 432 rather than being truncated to
  `NICKLEN=9` — RFC 2812 §3.1.2, and the grammar's own `nickname` production
  caps at 9. Covered by `test_conformance.cpp::NickLength`
  (`OverlongNickIsRejectedNotTruncated`, `NickOfExactlyNicklenIsAccepted`,
  `OverlongNicksCannotCollideByTruncation`, and the post-registration case).
  Known cost, accepted deliberately: HexChat's collision retry appends to the
  nick (`_`, `_1`), which only makes an over-long nick longer, so such a
  client cannot recover from a 432 on its own.

### 3.2 PRIVMSG fully functional with different parameters
- **Sheet**: PRIVMSG works with various parameters.
- **What proves it**: `test_integration.cpp` — `PrivateMessage`, `ChannelMessage`,
  plus error cases 411 (no recipient), 412 (no text), 401 (no such nick), 404
  (not on channel). ⟨verificar test names⟩
- **Trap (nc syntax)**: the space before `:` is mandatory — `PRIVMSG bob :hi`, not
  `PRIVMSG bob:hi` (→ 412). HexChat builds this for you; from nc you type it.

---

## 4. Client commands — channel operator (sheet: rated 0–5)

- **Sheet**: a regular user must **not** have operator privileges; an operator must.
  Test **all** operator commands (−1 point each broken feature).
- **What proves it**: `test_integration.cpp` operator suite ⟨verificar⟩ —
  positive (operator can) and negative (regular user denied `482 ERR_CHANOPRIVSNEEDED`):
  - **KICK** — `KickUser` / `KickDeniedForNonOperator`
  - **INVITE** — `InviteToChannel` / `InviteDeniedForNonOperatorWhenInviteOnly`
  - **TOPIC** — `TopicSetAndQuery` / `TopicDeniedForNonOperatorWhenRestricted`
  - **MODE i/t/k/o/l** — `ChannelMode{Query,Key,Limit}` / `ModeDeniedForNonOperator`
- **The five modes**: `+i` invite-only, `+t` op-only topic, `+k` key, `+o` grant op,
  `+l` user limit. `+i` end-to-end enforcement: `JoinInviteOnlyDeniedWithoutInvite`
  (473 on JOIN, 404 on PRIVMSG proves non-membership).
- **Defended live**: as a regular user, try KICK → `482`; op the user, retry → works.
  Do each of the four (KICK/INVITE/TOPIC/MODE) so no point is lost.

---

## 5. Bonus (only scored if mandatory is perfect)

### 5.1 File transfer with the reference client
- **Sheet**: file transfer works with HexChat.
- **What proves it**: `test_filetransfer.cpp` (10 tests incl. DCC relay byte-for-byte).
  Two paths — the server-mediated `FILE` protocol (base64 relay, never touches disk)
  and DCC passthrough (relays the `\x01`-CTCP payload untouched so HexChat's own DCC
  works). Live: HexChat→HexChat DCC send; the USER-GUIDE covers both.

### 5.2 A bot
- **Sheet**: there's an IRC bot.
- **What proves it**: `test_bot.cpp` — `ircbot` is a virtual in-server user answering
  `!help`/`!time`/`!info`/`!joke`; its nick is reserved (433), unknown command gets a
  reply not silence.

---

## Appendix — the verification layers, and what each one is for

Five distinct tools; each covers something the others don't. Knowing which is which
answers "how do you know your code is correct?"

| Layer | Command | What it verifies | Not covered by it |
|---|---|---|---|
| **Subject audit** | `bash scripts/audit.sh` | The eliminatory rules: C++98, `-Werror` clean, single epoll, fcntl flags, forbidden funcs, Makefile rules, no-relink | Runtime behaviour |
| **Sequential build** | `make verify-tiers` | All 3 tiers compile clean, serially (RAM-capped) | Anything not compile-time |
| **Test suite** | `cd tests && make` | ~155 product tests / 511 assertions: parser, channels, operators, PRIVMSG, robustness (T6), conformance (T1), bonus | The "reads like a real server" feel; DCC-with-HexChat |
| **Leak gate** | `bash scripts/memcheck.sh --auto` | No leaks across register/JOIN/msg/part/quit + abrupt disconnect, under valgrind | Logic correctness (it checks memory, not answers) |
| **Whitespace/style** | `bash scripts/normalize.sh --check` | Formatting (advisory — clang-format differences on aligned tables are expected) | Anything semantic |

**Why the "511 assertions" figure is not "511 tests"**: PostMan (the test reporter)
counts assertions. ~155 are real product tests; the other ~301 are
`PostManTruncationRegression`'s own self-check (that the reporter doesn't truncate
rows — the T7 fix). Real IRC coverage is the ~155.

**What only a human can verify** (no automated test — defend these live):
- poll-before-every-I/O and no-errno-driven-logic (§0.2) — walk the loop.
- "reads like a real IRC server" with HexChat — the reference-client feel.
- DCC file transfer end-to-end in HexChat (§5.1).
- Multiple clients (nc + HexChat) simultaneously without blocking (§1.3).

For these, the USER-GUIDE and `tests/MANUAL-TESTING.md` are the rehearsal scripts.
