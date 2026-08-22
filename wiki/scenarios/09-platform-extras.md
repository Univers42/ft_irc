# 09 — Platform extras

**Context.** Beyond the subject: an audit trail, a way for external services to
post into channels, and a Docker stack with AI and real-time companions.

Everything here is **doubly gated**. It is linked only into the full-tier
binary (`make`, the default), *and* it activates only when the environment
variable `FT_IRC_CONFIG` points at an INI file. Without that variable, the
default binary behaves **byte-for-byte** like the bonus tier. Defend on
`make mandatory`; this page is for the "what else did you build" conversation.

---

## Turning them on

```ini
# ft_irc.ini
[bus]
enabled = true
port    = 6710
secret  = s3cret
nick    = platform

[audit]
enabled = true
path    = ./audit.csv
```

```bash
FT_IRC_CONFIG=$PWD/ft_irc.ini ./ircserv 6667 mypass
```

---

## The audit log

An append-only CSV of everything that mattered:

```csv
timestamp,event,actor,detail
2026-08-21T14:46:44,register,alice,a@127.0.0.1
2026-08-21T14:46:44,join,alice,#general
2026-08-21T14:46:45,publish,platform,#general deploy
2026-08-21T14:46:47,disconnect,alice,Quit
```

Registrations, joins, parts, kicks, mode changes, publishes and disconnects.
The kernel calls one `audit()` hook and knows nothing about CSV, files, or
whether anything is listening.

---

## The platform bus

A **loopback-only** TCP socket, multiplexed into the same `epoll` loop as every
IRC client — not a second thread and not a second event loop. It lets an
external process inject messages into a channel.

Line protocol:

```
AUTH <secret>
PUB <#channel> <type> :<message>
```

Captured end to end:

```bash
printf 'AUTH s3cret\nPUB #general deploy :build 42 shipped to prod\n' \
  | nc 127.0.0.1 6710
```
```
OK authenticated
OK
```

Every member of `#general` sees:

```
:platform!svc@ft_irc PRIVMSG #general :[deploy] build 42 shipped to prod
```

The messages appear as coming from a virtual `platform` user — there is no
connection behind that nick. Use it for CI results, deploy notifications,
alerts: anything a service wants to say to a room.

Bound to loopback and gated behind a shared secret compared in constant time.
The secret must not be empty in any deployment you care about — an empty one
means "trust anything that can reach loopback".

---

## Docker

```bash
cp .env.example .env      # set ANTHROPIC_API_KEY, and a password
docker compose up --build
```

Starts `ircserv` on `${IRC_PORT:-6667}` plus the **ai-assistant** companion — a
separate Rust process that connects as an ordinary IRC client (nick
`assistant`), joins `$IRC_CHANNELS`, and answers when addressed:

```
!ai explain channel modes
assistant: summarise the backlog
/msg assistant hello
```

The C++ server contains no AI code and does not know the companion is one. All
outbound IRC lines funnel through a single writer task, so a multi-second model
call never delays PING/PONG.

It is more than a question-answer bot: it reads channel scrollback, WHOs the
roster, reads topic and modes, searches the web, and — when explicitly enabled —
moderates. That has its own page: [10 — The AI assistant](10-ai-assistant.md).

Server only, no companion:

```bash
docker build -t ircserv .
docker run --rm -p 6667:6667 ircserv 6667 mypassword
```

Test suite in a clean container:

```bash
docker build --target test -t ircserv-test .
```

### Real-time / web tier

```bash
docker compose --profile platform up --build
```

Adds **realtime-agnostic** (a WebSocket pub/sub engine with database
change-capture) and **realtime-bridge**, which mirrors IRC and realtime in both
directions. IRC messages publish to realtime under `irc:**`, which the bridge
never subscribes to — the loop is broken by namespace, not by a filter that
could be got wrong. Browser chat comes back the other way under
`irc-in/<channel>`, each web user appearing under **their own IRC nick** via a
short-lived puppet connection; database CDC events (`pg/**`, `mongo/**`) arrive
through the `rtbridge` client.

Purely additive: the default `docker compose up` is unchanged, and none of it
is part of the 42 build.

---

## Checks

* Without `FT_IRC_CONFIG`, a scripted session transcript is identical across
  all three binaries.
* With it, `audit.csv` gains a row per lifecycle event.
* `AUTH` + `PUB` answer `OK`; a wrong secret does not.
* The bus port is bound on `127.0.0.1` only — check with `ss -ltn`.
* `PUB` reaches every channel member as a `platform` PRIVMSG.

**Back to:** [scenario index](README.md) · [commands reference](commands.md)
