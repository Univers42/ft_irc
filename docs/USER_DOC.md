# ft_irc — User Guide

An IRC server in C++98 speaking a working subset of RFC 2812. Any standard
client (HexChat, Irssi, Textual, WeeChat) connects to it the way it would to a
public network.

---

## Prerequisites

* A compiled `ircserv` executable — `make`
* An IRC client (HexChat, Irssi, Textual) or `nc` for raw testing

## Launching the Server

```bash
./ircserv <port> <password>
./ircserv 6667 mysecretpassword
```

* **port** — 1–65535
* **password** — the server password every client must send. Cannot be empty.

Stop with `Ctrl+C`. For other machines on your LAN, share the address from
`hostname -I` instead of `127.0.0.1`.

## Connecting via IRC Client

| Field | Description | Example |
| --- | --- | --- |
| **Server Address** | `localhost` or `127.0.0.1` | `127.0.0.1` |
| **Port** | The port passed to `./ircserv` | `6667` |
| **Server Password** | The password passed to `./ircserv` | `mysecretpassword` |
| **Nickname** | Your chat handle — **9 characters max** | `alice` |

To watch the raw protocol underneath HexChat: **Window → Raw Log**.

---

## Registration

Three commands, in order. Your client sends them for you; from `nc` you type
them yourself:

```
PASS mysecretpassword
NICK alice
USER alice 0 * :Alice Liddell
```

Until all three arrive and the password matches, every other command answers
`451 :You have not registered`. A wrong password gets `464` and then a
disconnect.

---

## Essential Commands

**Authentication**

* `/PASS <password>` — Authenticate before registering.
* `/NICK <nickname>` — Set or change your nickname.
* `/USER <username> 0 * :<realname>` — Complete registration.
* `/QUIT [reason]` — Disconnect, telling your channels why.

**Channels**

* `/JOIN #<channel> [key]` — Join, or create it if new (creator becomes operator).
* `/PART #<channel> [reason]` — Leave.
* `/TOPIC #<channel> [new_topic]` — View or set the topic.
* `/MSG <target> <message>` — Message a user or channel (raw: `PRIVMSG <target> :<msg>`).
* `/NOTICE <target> <message>` — Same, but flagged "do not auto-reply".

**Operator**

* `/MODE #<channel> <flags> [args]` — Change channel modes.
* `/INVITE <nickname> #<channel>` — Invite a user.
* `/KICK #<channel> <nickname> [reason]` — Remove a user.

**Queries**

* `/WHO #<channel>` — Who is in a channel.
* `/WHOIS <nickname>` — User, host, real name, channels.
* `/USERHOST <nickname>` — Compact `nick=+user@host`.

> **Raw syntax note.** From `nc`, the space before `:` is mandatory — the colon
> opens the final parameter, the only one that may contain spaces.
> `PRIVMSG bob :hi` works; `PRIVMSG bob:hi` gets `412 :No text to send`.

---

## Channel Modes

* **`+i` / `-i`** — Invite-only.
* **`+t` / `-t`** — Topic restricted to operators.
* **`+k <key>` / `-k`** — Channel password. 1–23 chars, no space/comma/control.
* **`+o <nick>` / `-o`** — Grant or revoke operator.
* **`+l <n>` / `-l`** — Member limit, 1–65535.

`/MODE #channel` with no flags reads the current modes — **members only**, since
the reply carries the `+k` key.

---

## Common Workflows

**Start a channel and talk**

```
/join #general          -> you join as operator (@alice in the names list)
hello everyone          -> PRIVMSG #general :hello everyone
```

**Delegate and moderate**

```
/mode #general +o bob            grant bob operator
/mode #general +t                lock the topic to operators
/topic Project planning          set it
/kick #general spammer rules     remove someone
```

**Lock the room down**

```
/mode #general +i                invite-only  — outsiders get 473
/invite bob #general             bob can now join
/mode #general +k secret         key required — outsiders get 475
/join #general secret            how bob joins now
/mode #general +l 20             cap at 20    — full channel gives 471
```

You must be *in* a channel to invite to it, and in a `+i` channel only
operators may. An invite dies with the connection — it cannot be inherited by
whoever takes the nick next.

**Stay connected**

The server pings every ~2 minutes and drops you after ~2 more without an
answer. Clients handle this automatically; from `nc`, reply by hand:

```
:ft_irc PING :ft_irc     <- server
PONG :ft_irc             <- you
```

---

## Bonus: the Bot

`ircbot` is a virtual user inside the server. It answers **private messages
only** — `!help` typed into a channel is just channel text.

```
/msg ircbot !help
```

* `!help` — the command list
* `!time` — current server time
* `!info [#channel]` — server info, or a channel's member count and modes
* `!joke` — a joke

The nick is reserved, so nobody can impersonate it.

## Bonus: FILE Transfer

The server relays base64 chunks between two clients — it never decodes them and
never touches disk.

```
alice: FILE SEND bob hello.txt 12     bob receives: FILE OFFER 1 hello.txt 12
                                      bob: FILE ACCEPT 1
alice: FILE DATA 1 aGVsbG8gd29ybGQh   bob receives: FILE DATA 1 aGVsbG8gd29ybGQh
alice: FILE END 1                     bob receives: FILE END 1 12
```

* `FILE SEND <nick> <file> <size>` · `FILE ACCEPT|REJECT <id>` ·
  `FILE DATA <id> <base64>` · `FILE END <id>` · `FILE ABORT <id>`
* Use the id the server gave you — it increments per offer.
* Errors come back as `NOTICE`, since `FILE` has no RFC numeric.
* `FILE WAIT <id>` means the receiver is behind — pause, then continue.
  Idle 60 s aborts the transfer.

**HexChat DCC** also works: the server relays the CTCP payload untouched.

> Both require `make bonus` or `make`. On the `make mandatory` binary,
> `/msg ircbot !help` returns `401` and `FILE` returns `421`.

---

## Limits

| Thing | Limit | On excess |
| --- | --- | --- |
| Nickname | 9 chars | **Truncated**, not rejected |
| Channel name | `#` + 2–50 chars, no space/comma | `403` |
| Channel key | 1–23 chars | `525` |
| Member limit | 1–65535 | `696` |
| Topic | 390 chars | Truncated |
| Message line | 512 bytes | Truncated |
| Clients | 1024 | Connection closed, no message |

Nicknames and channels are **case-insensitive** — `Alice` and `alice` are the
same person. Your original spelling is kept for display.

---

## Troubleshooting

* **Connection refused** — `ircserv` isn't running, the port is taken, or a
  firewall is blocking it.
* **Dropped right after connecting** — wrong server password; look for `464`.
* **Everything answers `451`** — registration is incomplete. Send `PASS`,
  `NICK` *and* `USER`.
* **Your nick is shorter than you typed** — capped at 9 and truncated silently.
  `probeclient` is reachable as `probeclie`.
* **`JOIN` answers `473` / `475` / `471`** — the channel is invite-only, keyed,
  or full.
* **`/mode #channel` answers `442`** — only members can read channel modes.
* **Dropped with `Ping timeout` from `nc`** — nothing answered the server's
  `PING`.

---

## More

* `README.md` — build tiers, Docker, project summary
* `wiki/DOCUMENTATION.md` — internal design
* `tests/README.md` — test suites
