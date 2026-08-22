# 03 — Messaging & queries

**Context.** Two people are registered. They want to talk — in a channel, and
privately — and to find out who the other one is.

---

## HexChat

* Channel: type in the input box and press Enter. HexChat turns it into
  `PRIVMSG #general :your text`.
* Private: `/msg bob hello`, or double-click `bob` in the member list.
* `/notice bob hello` sends a NOTICE — same delivery, but flagged
  "do not auto-reply". Bots use it so two bots cannot loop forever.
* `/whois bob`, `/who #general` — output lands in the current tab.

---

## netcat — channel message

```
PRIVMSG #general :hello everyone
```

Everyone **else** in the channel receives:

```
:alice!alice@127.0.0.1 PRIVMSG #general :hello everyone
```

You do **not** get your own message back. That is deliberate and standard: your
client already displayed it locally when you pressed Enter, and echoing would
double it.

## netcat — private message

```
PRIVMSG bob :meet me in #ops
```

Bob alone receives it. Nick matching is ASCII case-insensitive, so
`PRIVMSG PROBECLIE :…` reaches `probeclie`:

```
:bob2!q@127.0.0.1 PRIVMSG PROBECLIE :case-insensitive delivery
```

Multiple targets in one command:

```
PRIVMSG #dev,#ops,bob :standup in 5
```

## NOTICE

Identical routing, different verb:

```
NOTICE #general :maintenance at 18:00
```

The one rule that matters: **NOTICE never generates an automatic error reply.**
`NOTICE ghost :hi` to a nonexistent nick is silently dropped, where
`PRIVMSG ghost :hi` answers `401`. Without that rule, two error-replying bots
bounce messages off each other forever.

---

## The colon, and why messages get truncated

```
PRIVMSG bob :hi there
```

The space before `:` is **mandatory**. The colon opens the *trailing*
parameter — the only one allowed to contain spaces. Without it:

```
PRIVMSG bob:hi
```
```
:ft_irc 412 n1 :No text to send
```

The whole `bob:hi` parsed as a single target, leaving no text.

Lines are capped at **512 bytes including CRLF**. Longer inbound lines are
truncated at 512 and the remainder is discarded *through* its terminator —
padding cannot be used to smuggle a second command past the limit. Outbound,
the cap is applied once, at the moment a line is queued, because the server
re-frames your text with a `:nick!user@host PRIVMSG #chan :` prefix — a legal
inbound line can become an illegal outbound one.

---

## Messaging errors

| Command | Reply |
| --- | --- |
| `PRIVMSG` | `:ft_irc 411 n1 :No recipient given (PRIVMSG)` |
| `PRIVMSG n1` | `:ft_irc 412 n1 :No text to send` |
| `PRIVMSG nobody :hi` | `:ft_irc 401 n1 nobody :No such nick/channel` |
| `PRIVMSG #notjoined :hi` | `404 :Cannot send to channel` |
| `FOOBAR` | `:ft_irc 421 n1 FOOBAR :Unknown command` |

You must be **in** a channel to send to it. That is what stops a drive-by
spammer from broadcasting into every room without joining one.

---

## Queries — who is that?

### WHOIS

```
WHOIS alice
```
```
:ft_irc 311 alice alice alice 127.0.0.1 * :Alice Liddell
:ft_irc 312 alice alice ft_irc :ft_irc server
:ft_irc 319 alice alice :@#general
:ft_irc 318 alice alice :End of WHOIS list
```

`311` is nick / user / host / realname, `319` the channels they are on with
their status prefix (`@` = operator there), `318` closes the block.

### WHO

```
WHO #general
```
```
:ft_irc 352 alice #general alice 127.0.0.1 ft_irc alice H@ :0 Alice Liddell
:ft_irc 315 alice #general :End of WHO list
```

One `352` per member. `H` = here (not away), `@` = channel operator.

### USERHOST

```
USERHOST alice
```
```
:ft_irc 302 alice :alice=+alice@127.0.0.1
```

The compact form — up to five nicks per query.

> **Only reachable users answer.** Queries and message delivery resolve nicks
> through a lookup that skips connections which have not finished registering
> and ones already tearing down. An unregistered connection never passed the
> `PASS` gate; a tearing-down one is about to have its fd closed and recycled,
> and binding a session to it would leak the next client's traffic.

---

## Checks

* Sender does **not** receive their own channel message; every other member does.
* `NOTICE ghost :x` produces no reply at all; `PRIVMSG ghost :x` produces `401`.
* `PRIVMSG` into a channel you have not joined gives `404`.
* `WHOIS` returns 311 → 312 → 319 → 318, in that order.

Guarded by `tests/05_privmsg.sh` and `tests/test_integration.cpp`.

**Next:** [04 — Operators & modes](04-operators-and-modes.md)
