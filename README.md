*This project has been created as part of the 42 curriculum by dlesieur, rlobun, <user vadim>.*

# ft_irc

An **IRC server in C++98** (RFC 2812), built for the 42 curriculum with
**HexChat** as the reference client. Single-threaded, non-blocking, everything
multiplexed through **one `epoll()` loop** — no thread and no process per
client.

**📖 [Full wiki →](wiki/README.md)** · scenarios, command reference,
architecture, attack playbook.

---

## Quick start

```bash
git submodule update --init --recursive   # fresh clone only
make all                                  # bare `make` prints the help screen
./ircserv 6667 mypass
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

`:ft_irc 001 alice :Welcome to the ft_irc Network …` means you are registered.

**HexChat:** Network List (`Ctrl+S`) → **Add** → **Edit** → server
`127.0.0.1/6667`, server password in **Password** (not "Nickserv password"),
nick **≤ 9 characters**, SSL off → **Connect**. Watch the wire underneath with
**Window → Raw Log**.

---

## Build

```bash
make mandatory   # strictly the subject's mandatory part — defend on this one
make bonus       # + Bot, FILE transfer
make             # full (default): + platform extras, runtime-gated
make re          # rebuild · make clean / fclean
make verify-tiers   # build all three in strict sequence
```

All three tiers produce the same `ircserv` from the same kernel sources and
differ **only at link time** — one `registerExtensions()` translation unit per
tier, zero `#ifdef`. The full tier's extras are additionally gated behind the
`FT_IRC_CONFIG` environment variable, so without it the default binary behaves
byte-for-byte like the bonus tier.

```bash
./ircserv <port> <password>     # port 1–65535, password non-empty
```

### Watching the protocol

```bash
FT_IRC_LOG=trace ./ircserv 6667 mypass
```

Prints every line crossing the socket, both directions, in RFC 2812 syntax
with the numerics named — passwords and channel keys redacted. Levels:
`quiet` `error` `warn` `info` (default) `debug` `trace`. See
[wiki/LOGGING.md](wiki/LOGGING.md).

---

## Features

**Core** — PASS/NICK/USER registration with a timing-safe password check ·
channels with `#` · PRIVMSG / NOTICE · KICK, INVITE, TOPIC, MODE ·
WHO / WHOIS / USERHOST · PING/PONG keepalive · partial-message reassembly.

**Channel modes** — `+i` invite-only · `+t` topic locked to operators ·
`+k` key · `+o` operator · `+l` member limit.

**Bonus** — `ircbot` (`!help` `!time` `!info` `!joke`, private messages only) ·
`FILE` transfer, a server-mediated base64 relay that never decodes and never
touches disk · DCC passthrough.

**Hardened** — ASCII casemapping · CR/LF/NUL line-injection sanitizer ·
bounded send queues and connection caps · invites keyed by connection rather
than by nickname · every reply echoing the server's canonical stored form.

**Limits** — nick 9 (truncated, not rejected) · channel 50 · key 23 ·
topic 390 · line 512 incl. CRLF · sendq 64 KiB/client · 1024 clients ·
ping 120 s + 120 s.

---

## Scenarios — one page per context of use

Each has a HexChat path, a **real captured** `netcat` transcript, and what to
check.

| # | Page | Context |
| --- | --- | --- |
| 01 | [First connection](wiki/scenarios/01-first-connection.md) | Launch, register, wrong password, nick rules |
| 02 | [Channels](wiki/scenarios/02-channels.md) | Create, join, topic, names, part |
| 03 | [Messaging & queries](wiki/scenarios/03-messaging.md) | Channel talk, private messages, NOTICE, WHO/WHOIS |
| 04 | [Operators & modes](wiki/scenarios/04-operators-and-modes.md) | `+o +t +i +k +l`, KICK, INVITE |
| 05 | [Multiple users](wiki/scenarios/05-multiple-users.md) | Collisions, casemapping, concurrency |
| 06 | [The bot](wiki/scenarios/06-bot.md) | `ircbot` — bonus |
| 07 | [File transfer](wiki/scenarios/07-file-transfer.md) | `FILE` relay, DCC — bonus |
| 08 | [Failure & resilience](wiki/scenarios/08-resilience.md) | Split packets, kills, timeouts, floods, leaks |
| 09 | [Platform extras](wiki/scenarios/09-platform-extras.md) | Audit log, platform bus, Docker, AI companion |

Plus the flat [**command reference**](wiki/scenarios/commands.md) — every
command, its raw syntax, its HexChat equivalent, and the numerics it answers
with.

---

## Testing

```bash
make test                             # Google Test suite, in-process
cd tests && bash ./run_all.sh         # black-box shell suite vs a live server
cd tests && ./run_dual.sh             # same suite under bash + hellish, diffed
bash scripts/audit.sh            # subject-compliance audit
bash scripts/memcheck.sh --auto  # Valgrind gate: 0 clean / 97 leak / 90 unverified
make norm                             # style gate
```

Two suites proving different things: Google Test exercises the classes in
process, the shell suite drives a live `./ircserv` over TCP and can do things
an in-process test cannot — split a command across packets, `kill -9` a client
mid-sentence, freeze a reader with `Ctrl+Z`.
See [`tests/README.md`](tests/README.md).

---

## Docker

```bash
cp .env.example .env          # set ANTHROPIC_API_KEY and a password
docker compose up --build     # ircserv + the AI companion
```

Starts `ircserv` on `${IRC_PORT:-6667}` plus **ai-assistant**, a separate Rust
process that connects as an ordinary IRC client (nick `assistant`) and answers
when addressed — `!ai …`, `assistant: …`, or a direct message. The C++ server
contains no AI code and is unaware of it.

```bash
docker build -t ircserv . && docker run --rm -p 6667:6667 ircserv 6667 mypassword
docker build --target test -t ircserv-test .        # test suite in a container
docker compose --profile platform up --build        # + realtime bridge tier
```

The `--profile platform` tier adds a WebSocket pub/sub engine and a
bidirectional IRC↔realtime bridge. Everything under `companions/` is outside
the 42 build. Secrets live only in the gitignored `.env`.

---

## Documentation

| File | What it covers |
| --- | --- |
| [wiki/README.md](wiki/README.md) | Wiki index — start here |
| [wiki/scenarios/](wiki/scenarios/README.md) | Scenarios and command reference |
| [wiki/USER-GUIDE.md](wiki/USER-GUIDE.md) | Feature-by-feature prose walkthrough |
| [wiki/LOGGING.md](wiki/LOGGING.md) | Server-side protocol trace and log levels |
| [wiki/DOCUMENTATION.md](wiki/DOCUMENTATION.md) | Architecture, extension seam, protocol details |
| [wiki/ATTACK.md](wiki/ATTACK.md) | Adversarial playbook |
| [wiki/DEFENSE-MAP.md](wiki/DEFENSE-MAP.md) | Subject obligations → the command that proves each |
| [tests/TESTING.md](tests/TESTING.md) | QA discipline |
| `subject.txt` / `en.subject.pdf` | The assignment |

## Resources

[RFC 2812](https://datatracker.ietf.org/doc/html/rfc2812) ·
[RFC 1459](https://datatracker.ietf.org/doc/html/rfc1459) ·
[Modern IRC](https://modern.ircdocs.horse/) ·
[IRCv3](https://ircv3.net/irc/) ·
[HexChat docs](https://hexchat.readthedocs.io/) ·
[epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html)

### AI usage

AI (GitHub Copilot with Claude) was used as a programming assistant for:
planning the architecture and identifying the numerics HexChat needs;
generating boilerplate for class declarations and socket setup; implementing
command handlers against RFC 2812; and debugging protocol-compliance issues
during testing.
