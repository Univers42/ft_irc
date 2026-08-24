# Scenarios — ft_irc in use

Each page here is one **context of use**: what you are trying to do, the
HexChat path, the raw `netcat` path, and what the server must answer. Every
transcript on these pages was **captured from a running `./build/bin/ircserv`**, not
written from memory — you can replay them line for line.

## How to read a scenario

Each page follows the same four beats:

| Beat | What it gives you |
| --- | --- |
| **Context** | The situation, and why the server behaves this way |
| **HexChat** | The click-and-type path a normal user takes |
| **netcat** | The exact bytes on the wire, `^M` = the CR of CRLF |
| **Checks** | What proves it worked, plus the test that guards it |

Two conventions used throughout:

* Server address `127.0.0.1`, port `6667`, password `mypass`. Change to taste.
* `nc -C` is mandatory: `-C` sends **CRLF**, which is what IRC frames lines
  with. Without it some builds send bare `\n`; this server tolerates that, but
  you stop seeing real protocol.

## The scenarios

| # | Page | Context |
| --- | --- | --- |
| 01 | [First connection](01-first-connection.md) | Start the server, register, survive a wrong password |
| 02 | [Channels](02-channels.md) | Create, join, topic, names, part |
| 03 | [Messaging & queries](03-messaging.md) | Channel talk, private messages, NOTICE, WHO/WHOIS/USERHOST |
| 04 | [Operators & modes](04-operators-and-modes.md) | Moderate a room: `+o +t +i +k +l`, KICK, INVITE |
| 05 | [Multiple users](05-multiple-users.md) | Nick collisions, casemapping, many clients at once |
| 06 | [The bot](06-bot.md) | `ircbot` — bonus tier |
| 07 | [File transfer](07-file-transfer.md) | `FILE` relay and HexChat DCC — bonus tier |
| 08 | [Failure & resilience](08-resilience.md) | Split packets, abrupt kills, ping timeout, floods, shutdown |
| 09 | [Platform extras](09-platform-extras.md) | `FT_IRC_CONFIG`: audit trail, platform bus, Docker |
| 10 | [The AI assistant](10-ai-assistant.md) | `assistant`: the agentic Claude companion — outside the 42 build |

Plus [**commands.md**](commands.md) — the flat reference: every command, its
raw syntax, its HexChat equivalent, and the numerics it can answer with.

## Fastest possible start

```bash
make && ./build/bin/ircserv 6667 mypass       # terminal 1
nc -C 127.0.0.1 6667                # terminal 2
```

```
PASS mypass
NICK alice
USER alice 0 * :Alice Liddell
JOIN #general
PRIVMSG #general :hello
```

If you see `:ft_irc 001 alice :Welcome to the ft_irc Network …`, everything
below this line will work.
