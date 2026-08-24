# ft_irc — Wiki

An IRC server in **C++98**, RFC 2812, single-threaded, non-blocking, one
`epoll` loop. Built for the 42 curriculum with **HexChat** as the reference
client.

Start here depending on who you are:

| You want to… | Go to |
| --- | --- |
| **Use it** — connect and chat | [Scenarios](scenarios/README.md) |
| Look up a command | [Command reference](scenarios/commands.md) |
| A prose walkthrough of every feature | [USER_DOC.md](USER_DOC.md) |
| **Watch the server talk to its clients** | [LOGGING.md](LOGGING.md) |
| **Understand how it works** | [NETWORKING.md](NETWORKING.md) · [GRAMMAR-ARCHITECTURE.md](GRAMMAR-ARCHITECTURE.md) |
| **How one thread serves many clients** — epoll, TCP, no locks | [NETWORKING.md](NETWORKING.md) |
| How IRC lines are parsed — scanner, LL parser, AST, matchers | [GRAMMAR-ARCHITECTURE.md](GRAMMAR-ARCHITECTURE.md) |
| The fast matcher explained from scratch | [THOMPSON-NFA.md](THOMPSON-NFA.md) |
| **Check it against RFC 2812 / the subject** | [RFC-CONFORMANCE.md](RFC-CONFORMANCE.md) |
| Read the RFC notes it is built against | [IRC_client_protocol.md](IRC_client_protocol.md) |
| **Prepare for the defense** — the whole eval sheet, point by point | [DEFENSE-PLAYBOOK.md](DEFENSE-PLAYBOOK.md) |
| **Break it** on purpose | [DEFENSE-PLAYBOOK.md](DEFENSE-PLAYBOOK.md) |
| Prove a subject requirement | [DEFENSE-PLAYBOOK.md](DEFENSE-PLAYBOOK.md) |
| Run the tests | [../tests/README.md](../tests/README.md) · [TESTS.md](TESTS.md) |

---

## Quick start

```bash
git submodule update --init vendor/libcpp vendor/googletest   # fresh clone only
# NOT --recursive: libcpp pins a nested submodule whose commit was never pushed.
make                                      # or: make mandatory / make bonus
./build/bin/ircserv 6667 mypass
```

```bash
nc -C 127.0.0.1 6667
```
```
PASS mypass
NICK alice
USER alice 0 * :Alice Liddell
JOIN #general
PRIVMSG #general :hello
```

`:ft_irc 001 alice :Welcome to the ft_irc Network …` means you are in.

With HexChat: **Network List** → **Add** → server `127.0.0.1/6667`, put the
password in **Password**, nick ≤ 9 characters, **Connect**.

---

## Scenarios — one page per context of use

Each has a HexChat path, a real captured `netcat` transcript, and what to
check.

| # | Page | Context |
| --- | --- | --- |
| 01 | [First connection](scenarios/01-first-connection.md) | Launch, register, wrong password, nick rules |
| 02 | [Channels](scenarios/02-channels.md) | Create, join, topic, names, part |
| 03 | [Messaging & queries](scenarios/03-messaging.md) | Channel talk, private messages, NOTICE, WHO/WHOIS |
| 04 | [Operators & modes](scenarios/04-operators-and-modes.md) | `+o +t +i +k +l`, KICK, INVITE |
| 05 | [Multiple users](scenarios/05-multiple-users.md) | Collisions, casemapping, concurrency |
| 06 | [The bot](scenarios/06-bot.md) | `ircbot` — bonus |
| 07 | [File transfer](scenarios/07-file-transfer.md) | `FILE` relay, DCC passthrough — bonus |
| 08 | [Failure & resilience](scenarios/08-resilience.md) | Split packets, kills, timeouts, floods, leaks |
| 09 | [Platform extras](scenarios/09-platform-extras.md) | Audit log, platform bus, Docker |
| 10 | [The AI assistant](scenarios/10-ai-assistant.md) | `assistant` — the agentic Claude companion |

---

## Build tiers

Three binaries from the same kernel sources, differing **only at link time** —
one `registerExtensions()` translation unit each, no `#ifdef` anywhere.

| Command | Contains |
| --- | --- |
| `make mandatory` | Strictly the subject's mandatory part — **defend on this one** |
| `make bonus` | + Bot, FILE transfer |
| `make all` | + platform extras, all runtime-gated behind `FT_IRC_CONFIG` |
| `make help` | Every target, tier and overridable flag — this is what bare `make` prints |

Without `FT_IRC_CONFIG` the default binary is byte-identical in behaviour to
the bonus tier.

```bash
make verify-tiers   # build all three in strict sequence
```

---

## Cheat sheet

**Commands** — `PASS` `NICK` `USER` `QUIT` `CAP` · `JOIN` `PART` `TOPIC` ·
`PRIVMSG` `NOTICE` `PING` `PONG` · `KICK` `INVITE` `MODE` · `WHO` `WHOIS`
`USERHOST` · *(bonus)* `FILE`

**Modes** — `+i` invite-only · `+t` topic locked to operators · `+k` key ·
`+o` operator · `+l` member limit

**Limits** — nick 9 (over-length → 432) · channel 50 · key 23 · topic 390 · line 512 ·
sendq 64 KiB · clients 1024 · ping 120 s + 120 s

---

## Testing

```bash
scripts/simulation.sh                 # a populated server: 10 users, channels, ops
scripts/shutdown_simulation.sh        # free all of it
make test                             # Google Test, in-process
cd tests && bash ./run_all.sh         # black-box shell suite vs a live server
./run_dual.sh                         # same suite under bash + hellish, diffed
scripts/simulation.sh --fuzz-mode 400  # fuzz the MODE parser
scripts/simulation.sh --verify-grammar # RFC 2812 §2.3.1 message grammar
scripts/simulation.sh --verify-names   # nickname / channel productions
bash scripts/audit.sh            # subject-compliance audit
bash scripts/memcheck.sh --auto  # Valgrind gate (exit 0 / 97 leak / 90 unverified)
make norm                             # style gate
```

[TESTS.md](TESTS.md) collects the one-liner greps (single `epoll_wait`, `fcntl`
form, no forking); [`../tests/TESTING.md`](../tests/TESTING.md) is the QA
discipline — every regression test needs a recorded red state.

---

## Also here

* [RFC-CONFORMANCE.md](RFC-CONFORMANCE.md) — measured RFC 2812 §2.3.1 + subject conformance: 64 pass, 5 documented divergences, 0 failures
* [IRC_client_protocol.md](IRC_client_protocol.md) — index of the RFC 2812 extracts, and what the server does about each
* [LOGGING.md](LOGGING.md) — server-side protocol trace: every line both ways, in RFC 2812 syntax, credentials redacted
* [NETWORKING.md](NETWORKING.md) — one thread, many clients, no locks
* [GRAMMAR-ARCHITECTURE.md](GRAMMAR-ARCHITECTURE.md) — scanner, parser, AST, the two matchers
* [USER_DOC.md](USER_DOC.md) — feature-by-feature prose walkthrough
* [DEFENSE-PLAYBOOK.md](DEFENSE-PLAYBOOK.md) — adversarial playbook, and every subject obligation mapped to the command that proves it
* [set_channels_test.md](set_channels_test.md) — naming convention for manual test channels
* [../scripts/sim/README.md](../scripts/sim/README.md) — the simulation harness: ten users, HexChat or netcat, one command
* [non-blocking_i/o_loop.md](non-blocking_i/o_loop.md) — the event loop, in detail

## References

[RFC 2812](https://datatracker.ietf.org/doc/html/rfc2812) ·
[RFC 1459](https://datatracker.ietf.org/doc/html/rfc1459) ·
[Modern IRC](https://modern.ircdocs.horse/) ·
[IRCv3](https://ircv3.net/irc/) ·
[HexChat docs](https://hexchat.readthedocs.io/) ·
[epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html)
