# RFC 2812 conformance — measured, not claimed

Every line on this page was produced by running `./ircserv` and reading what
came back. Nothing here is inferred from the source. Where the server departs
from RFC 2812 it says so, with the RFC citation and the file and line that
causes it.

Reproduce the whole thing:

```bash
make                                    # C++98, -Wall -Wextra -Werror
scripts/simulation.sh                   # a populated server in the background
scripts/simulation.sh --verify-grammar  # §2.3.1 message grammar
scripts/simulation.sh --verify-names    # §2.3.1 nickname / channel productions
scripts/simulation.sh --fuzz-mode       # MODE parser invariants
scripts/shutdown_simulation.sh
```

**Result: 68 pass · 1 documented divergence · 0 failures.**

Measured on the current build:

| probe | pass | diverge | fail |
| --- | --- | --- | --- |
| `--verify-grammar` | 22 | 1 | 0 |
| `--verify-names` | 46 | 0 | 0 |
| `--fuzz-mode` (227 cases) | — | — | 0 violations |

A *failure* means the server contradicts the RFC or its own `005` tokens. A
*divergence* means it is deliberately stricter or looser, and there is a
reason. There are no failures.

---

## Part 1 — §2.3.1, production by production

The grammar this section defines:

```abnf
message    =  [ ":" prefix SPACE ] command [ params ] crlf
prefix     =  servername / ( nickname [ [ "!" user ] "@" host ] )
command    =  1*letter / 3digit
params     =  *14( SPACE middle ) [ SPACE ":" trailing ]
           =/ 14( SPACE middle ) [ SPACE [ ":" ] trailing ]

nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF
middle     =  nospcrlfcl *( ":" / nospcrlfcl )
trailing   =  *( ":" / " " / nospcrlfcl )

SPACE      =  %x20
crlf       =  %x0D %x0A

nickname   =  ( letter / special ) *8( letter / digit / special / "-" )
chanstring =  %x01-07 / %x08-09 / %x0B-0C / %x0E-1F / %x21-2B
chanstring =/ %x2D-39 / %x3B-FF
key        =  1*23( %x01-05 / %x07-08 / %x0C / %x0E-1F / %x21-7F )
letter     =  %x41-5A / %x61-7A
digit      =  %x30-39
special    =  %x5B-60 / %x7B-7D        ; "[", "]", "\", "`", "_", "^", "{", "|", "}"
```

### `crlf` and message framing

| Rule | Measured | Status |
| --- | --- | --- |
| Messages terminated by CRLF | `JOIN #x\r\n` executes | pass |
| "Empty messages are silently ignored" (§2.3) | Bare `\r\n` between commands ignored; the next command still runs | pass |
| Several messages in one read | Three commands in one `write()` all executed, in order | pass |
| One message across several reads | `JO` + `IN #g` + `r3\r\n` reassembled into one JOIN | pass |
| Bare LF as terminator | **Accepted** | **diverge — see D2** |

The reassembly case is the subject's own `nc -C` test (`com`/`man`/`d`),
verified here in exactly that shape.

### `prefix`

| Rule | Measured | Status |
| --- | --- | --- |
| `[ ":" prefix SPACE ]` is optional and skipped | `:g5!u@host PRIVMSG …` runs PRIVMSG, not `:G5!U@HOST` | pass |
| A line that is only a prefix carries no command | `:onlyprefix\r\n` ignored, no `421` | pass |

`Message` has no `prefix` field on purpose — §2.3 says clients SHOULD NOT send
one, and this server has no server-to-server link that would make one
meaningful. The `message` production captures `$prefix` and the server drops
it when building parameters, so it cannot be mistaken for the command.

There is no hand-written parser to point at any more: `Message::parse` was
deleted once the grammar took over `Server::handleMessage`. Every row on this
page is now produced by the same engine the server runs on, which is the point
of the exercise — the document and the implementation cannot drift apart
without a probe going red.

### `command = 1*letter / 3digit`

| Rule | Measured | Status |
| --- | --- | --- |
| Commands are case-insensitive | `join`, `JoIn` both work | pass |
| A client sending a numeric | `001 foo` → `421 :Unknown command` | pass |

### `params`, `middle`, `trailing`

| Rule | Measured | Status |
| --- | --- | --- |
| `trailing` may contain SPACE | `:a b c   d` came back with its spacing intact | pass |
| `trailing` may contain `:` | `:see http://host:8080/x` preserved | pass |
| Present-but-empty trailing | `PRIVMSG gc :` → `412 :No text to send`, not a crash | pass |
| At most 15 parameters | 16 middles are **all parsed** | **diverge — see D3** |
| `:` inside a channel name | Kept as part of the name | **diverge — see D4** |

### Forbidden octets

`nospcrlfcl` excludes NUL, CR and LF, and note 2 of §2.3.1 says plainly that
"NUL is not allowed within messages."

| Rule | Measured | Status |
| --- | --- | --- |
| NUL inside a parameter | Stripped; the line still parses | pass |
| Stray CR inside a parameter | Stripped; a forged `\rQUIT` did **not** end the session | pass |

The second row is the line-injection defence, and it is the one that matters
most: without it any client could append `\rQUIT` — or a `\rKICK` — to a
message and have the server execute it. One sanitizer at
`src/Client.cpp:78` drops `\r` and `\0` from every extracted line.

### Length — 512 octets including CRLF

| Rule | Measured | Status |
| --- | --- | --- |
| A 512-octet line is legal | 510 payload + CRLF accepted, session continues | pass |
| Over-long lines are truncated | 600 octets of padding truncated | pass |
| The remainder is discarded through its terminator | A `JOIN #smuggled` hidden behind the padding was **not** executed | pass |

That last row is a real invariant, not a formality. If the tail of an
over-long line were framed as the next command, padding would smuggle a
command past the 512-byte limit. Inbound, `LineBuffer` never returns more than
`maxLine` bytes; outbound, `Client::queueMessage` (`src/Client.cpp:104`) caps
every queued line at `MAX_MSGLEN - 2`.

The outbound cap cannot live at the inbound edge, because the server re-frames
a client's line with a `:nick!user@host PRIVMSG #chan :` prefix before relaying
it — a legal inbound line becomes an illegal outbound one.

### `nickname` — 45 probes

`nickname = ( letter / special ) *8( letter / digit / special / "-" )`

Accepted, as required: `z1` · `Zed` (case preserved) · `z` · `z9` · `z-dash` ·
`[zbr]` · `{zbc}` · `z\bs` · `z|pipe` · `z^car` · `z_und`.

Rejected with `432`, as required: leading digit · leading dash · comma · `#` ·
`!` · `@` · `.` · `*` · `?` · non-ASCII. `NICK` with no parameter gives `431`.

`` ` `` is the one exception — see **D1**.

Length and collisions, against the advertised `NICKLEN=9` and
`CASEMAPPING=ascii`:

| Case | Measured |
| --- | --- |
| `abcdefghi` (9) | registers unchanged |
| `abcdefghij` (10) | **truncated** to `abcdefghi` |
| `probeclient` (11) | truncated to `probeclie` |
| `casetestx` held, then `CASETESTX` | `433` |
| `casetestxZZ` (11, truncates onto the held 9-char nick) | `433` |

The last row is the one worth keeping: it proves truncation happens *before*
the in-use check. Reverse those two steps and two different over-long nicks
collapse onto one name and both register.

Truncation rather than rejection is deliberate — HexChat's own collision retry
appends a suffix, which only makes an over-long nick longer, so rejecting it
would leave the client unable to connect at all.

### `chanstring` and channel names

Against the advertised `CHANTYPES=#` and `CHANNELLEN=50`: `#zz` (the 2-char
minimum), `#zz-dash`, `#zz_und` and a 50-character name are accepted; `zzplain`
(no prefix), `#` alone, `&zz` and a 51-character name are refused.

A comma splits a JOIN list into separate channels rather than forming a name.
`#ZzCaseChan` and `#zzcasechan` are the same channel, and the server echoes its
own stored spelling — a client that matches its channel list by string would
desync otherwise.

### `key`

| Rule | Measured | Status |
| --- | --- | --- |
| `1*23` octets | 23 accepted, 24 → `525` | pass |
| No space or control characters | refused | pass |
| Comma | refused | pass (**stricter**, and right — JOIN key lists are comma-separated) |
| 8-bit octets | **accepted** | **diverge — see D5** |

### MODE — parser surface

MODE is the widest parser surface here: a free-form sign/letter string plus a
positional parameter list, where each letter decides for itself whether it
consumes one. Fuzzed with `scripts/simulation.sh --fuzz-mode` (477 cases, nine
invariants), which found three defects, all since fixed:

| Rule | Measured | Status |
| --- | --- | --- |
| One reply per distinct complaint | `jfsadfsahf` → 6 × `472` for 6 distinct chars; `+ooo` → 1 × `461` | pass |
| Reply volume is bounded | 495 unknown chars → 1 line (was 495 lines / 23 KB, 47×) | pass |
| A consumed parameter is always echoed | `-k+o alice` → `-k+o alice`, the grant survives | pass |
| `-k` takes its argument only when spare | `-k oldkey` consumes it; `-k+o alice` does not | pass |
| `005` describes what is sent | `CHANMODES=,,kl,it` — both k and l in group C | pass |

The `k`-in-group-C choice is deliberate and was verified against HexChat 2.16
in both directions: under the conventional group B the client mis-parsed
`-k+o bob` and rendered an empty nick, while the server had opped bob
correctly. ISUPPORT cannot express "optional", so C is the group that matches
the wire.

---

## Part 2 — the one remaining divergence

Four of the five divergences this page used to list are closed. They are kept
here with what closed them, because "we used to differ and here is the commit"
is more useful than silence.

### D2 · a bare LF is accepted as a terminator — OPEN, deliberate

`crlf = %x0D %x0A`. A message ending in LF alone is not a message.

Measured: a full `PASS`/`NICK`/`USER`/`JOIN` session using only `\n` registers
and joins.

Deliberate, and the only one left. `nc` without `-C` sends bare LF, and the
subject's own test command is `nc -C 127.0.0.1 6667` typed by hand — being
strict here would make the server unusable from the very tool the subject
tells you to test with. Real ircds are lenient in exactly the same way.

### D1 · `` ` `` in a nickname — CLOSED

`special = %x5B-60 / %x7B-7D`. The range `%x5B-60` is nine characters —
`[ \ ] ^ _` **and backtick** (%x60). `Server::isValidNickname` enumerated them
by hand and listed eight.

Now checked against the RFC's ranges directly rather than a hand-written list,
which is what made the omission possible. The locale-sensitive `std::isalpha` /
`std::isalnum` went with it: under a non-C locale they would have admitted
8-bit octets into a nickname.

Measured: `NICK z\`tick` registers. `--verify-names` is 46 pass · 0 diverge.

### D3 · more than 15 parameters — CLOSED

`params = *14( SPACE middle ) [ SPACE ":" trailing ]`, with the `=/`
alternative folding everything past the fourteenth middle into the trailing.
The old hand-written parser looped to end-of-line with no counter.

The grammar implements both alternatives, so the cap is structural rather than
a check that could be forgotten. Note the RFC caps by ABSORPTION, not by
error — a conformant server does not refuse the line, it declines to produce a
sixteenth parameter. `--verify-grammar` asserted a 417/461 the RFC never asks
for and reported conformant behaviour as a divergence; that probe is fixed.

Measured: `PRIVMSG p1 … p16 :body` → `401` for `p1`, the surplus folded into
the message text.

### D4 · `:` inside a channel name — CLOSED

`Server::isValidChannelName` now rejects `:`, drawing the existing
`476 ERR_BADCHANMASK`.

Worth stating plainly: this makes the server **stricter than the RFC**, not
merely conformant. `channel = ( "#" / … ) chanstring [ ":" chanstring ]` means
`#a:b` is syntactically valid as name-plus-mask. Rejecting it is safe only
because masks are a server-to-server feature, which the subject forbids.

Measured: `JOIN #ok,#ba:d` joins `#ok` and answers `476` for `#ba:d`.

### D5 · 8-bit octets in a channel key — CLOSED

`key = 1*23( %x01-05 / %x07-08 / %x0C / %x0E-1F / %x21-7F )` is 7-bit ASCII.
The validator rejected `<= 0x20` and `,` but let anything above `0x7F` through.

It also lived as a `static` in `CommandOperator.cpp`, so `cmdJoin` could not
reach it and compared keys as raw strings — `MODE +k` and `JOIN` disagreed
about what a key even was. It is now a `Server` member used by both.

Measured: `MODE #k +k s<0xC3><0xA9>cret` → `525 :Key is not well-formed`.

## Part 3 — subject requirements, and the command that proves each

Run on the tier the subject is graded against unless noted.

### Build and general rules

| Requirement | Proof | Result |
| --- | --- | --- |
| Compiles with `-Wall -Wextra -Werror` | `make re` | **clean, zero warnings** |
| C++98 (`-std=c++98`) | in `CXXFLAGS`; `scripts/audit.sh` scans for C++11+ tokens | **pass** |
| Makefile has `$(NAME) all clean fclean re` | `scripts/audit.sh` | **pass** |
| No unnecessary relinking | second `make all` is a no-op | **pass** |
| No external or Boost libraries | nothing linked but the C++ runtime; libcpp is compiled from source | **pass** |
| Does not crash, does not quit unexpectedly | `tests/run_all.sh` (11 suites), `make test`, valgrind gate | **pass** |

### Networking rules

| Requirement | Proof | Result |
| --- | --- | --- |
| Exactly one `poll()`/equivalent for everything | `scripts/audit.sh` → "exactly 1 event-wait call site" | **1 `epoll_wait`** |
| Never read/write a fd without polling first | every send goes through `queueMessage`, drained on `EPOLLOUT` | **pass** |
| All I/O non-blocking | `fcntl` appears only as `fcntl(fd, F_SETFL, O_NONBLOCK)` | **pass** |
| Forking prohibited | `tests/no_forking.sh`; thread count stays 1 | **pass** |
| Multiple clients without hanging | `tests/10_stress_multiclient.sh` — 12 clients | **pass** |
| TCP/IP | plain IPv4 sockets | **pass** |

A naive `grep -c 'epoll_wait('` over `src/` returns **2**, because the error
string `"epoll_wait() failed: "` contains the text. It is a string literal, not
a second call site. The audit strips strings and comments before counting; a
snippet pasted from a cheatsheet does not, which is how a false "2 poll calls"
gets reported.

### Feature requirements

| Requirement | Verified by |
| --- | --- |
| Authenticate, set nickname and username | `tests/02_registration.sh`, [01 — First connection](scenarios/01-first-connection.md) |
| Join a channel | `tests/06_channel_join_part.sh`, [02 — Channels](scenarios/02-channels.md) |
| Send and receive private messages | `tests/05_privmsg.sh`, [03 — Messaging](scenarios/03-messaging.md) |
| Every channel message forwarded to all other members | `tests/10_stress_multiclient.sh` |
| Operators and regular users | [04 — Operators & modes](scenarios/04-operators-and-modes.md) |
| `KICK` | `tests/07_kick_invite_topic.sh` |
| `INVITE` | `tests/07_kick_invite_topic.sh` |
| `TOPIC` (view and change) | `tests/07_kick_invite_topic.sh` |
| `MODE i t k o l` | `tests/08_modes.sh` |
| Partial data / packet reassembly | `tests/03_tcp_framing.sh`, and the framing table above |
| Reference client connects with no error | HexChat 2.16 — `scripts/simulation.sh --hexchat 2` |

The reference client is **HexChat**, driven for real: the simulation harness
generates an isolated HexChat profile, launches the GUI, and it registers and
autojoins against `ircserv` with no error.

---

## Part 4 — what was actually run

| Gate | Command | Result |
| --- | --- | --- |
| Build | `make re` | clean, zero warnings |
| Subject audit, 3 tiers | `bash scripts/audit.sh` | **AUDIT PASSED** |
| Unit + integration | `make test` | **515 / 515 assertions** |
| Black-box shell suite | `cd tests && bash run_all.sh` | **11 / 11 suites OK** |
| Leak gate under Valgrind | `bash scripts/memcheck.sh --auto` | **exit 0**, `0 errors from 0 contexts` |
| Message grammar | `scripts/simulation.sh --verify-grammar` | 19 pass · 4 diverge · 0 fail |
| Name grammar | `scripts/simulation.sh --verify-names` | 45 pass · 1 diverge · 0 fail |

### The suite is green

`make test` reports **515 / 515 assertions passing**.

`DeferredCloseDeadlineTest.FrozenPeerClosedByDeadlineNotDrain` failed for a
long time and was carried as "pre-existing". It was a **test** defect, in three
compounding parts, and the server was correct throughout:

* The client clamped `SO_RCVBUF` *after* `connect()`. That resizes the buffer
  but not the window already advertised during the handshake, and a sender may
  burst up to it — so the "frozen" peer absorbed all 50 KB while the socket
  read back the requested 8 KiB. Measured with `SIOCOUTQ`: the server's kernel
  send queue was fully drained and ACKed.
* `checkClosed()` drained up to 100 x 4 KiB every 20 ms — about 20 MB/s —
  directly beneath a comment reading *"Deliberately never read: this is the
  frozen-peer side of the test."*
* The probe adopted the healthy heartbeat connection the test opens to keep the
  event loop ticking, and tore that down too.

With the backlog absorbed, the server closed on drain-completion at ~221 ms and
the test failed a 1000 ms deadline assertion — which reads as a server timing
bug and is not one.

Fixed by clamping the window before `connect()`, detecting closure with
`poll()` instead of by draining, and latching the probe onto one subject. The
deadline path now genuinely fires, and the repaired test was fault-injected
(deadline sweep disabled) to confirm it still fails when the mechanism it
guards is broken.

---

## See also

* [IRC client protocol notes](IRC_client_protocol.md) — the RFC extracts this
  page is checked against
* [DEFENSE-MAP.md](DEFENSE-MAP.md) — every evaluation-sheet obligation and the
  command that demonstrates it
* [scenarios/](scenarios/README.md) — the same behaviour as walkthroughs
* [`scripts/sim/verify_names.sh`](../scripts/sim/verify_names.sh) ·
  [`verify_grammar.sh`](../scripts/sim/verify_grammar.sh) — the probes
