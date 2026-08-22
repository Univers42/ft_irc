# 06 — The bot (`ircbot`)

**Context.** The subject's bonus asks for a bot. `ircbot` is a *virtual*
user: it has no socket and no connection, it lives inside the server as an
extension and claims messages addressed to its nick.

> **Requires `make bonus` or `make`.** On the `make mandatory` binary the nick
> is not reserved and `PRIVMSG ircbot :!help` answers `401 No such
> nick/channel`.

---

## HexChat

```
/msg ircbot !help
```

A private-message tab opens with the bot's reply in it. It appears in no member
list and in no `WHO` output, because it holds no connection — but its nick is
reserved, so `NICK ircbot` is refused and nobody can impersonate it.

---

## netcat

```
PRIVMSG ircbot :!help
```

```
:ircbot PRIVMSG alice :Available commands:
:ircbot PRIVMSG alice :  !help           - Show this help message
:ircbot PRIVMSG alice :  !time           - Show current server time
:ircbot PRIVMSG alice :  !info [#chan]    - Show server or channel info
:ircbot PRIVMSG alice :  !joke           - Tell a random joke
```

| Command | Answers with |
| --- | --- |
| `!help` | the list above |
| `!time` | current server time |
| `!info` | server info; `!info #chan` gives that channel's member count and modes |
| `!joke` | a joke, rotating through a fixed set |

---

## The one rule that surprises people

**The bot answers private messages only.** `!help` typed into a channel is just
channel text — the bot does not see it, and nobody gets a reply.

That is a design decision, not an omission. A bot that watches every channel
line has to be handed every message in the server; one that claims only
messages addressed to its own nick touches nothing else. It plugs into the
extension seam at exactly two points — "is this message for me?" and "is this
nick mine?" — and the RFC command handlers know nothing about it.

The same seam is why the bot can never shadow a real command: extensions are
consulted only where `ERR_UNKNOWNCOMMAND` would otherwise be sent, so an
extension can *add* verbs but never intercept `JOIN` or `PRIVMSG`.

---

## The other bot — the AI companion

`companions/ai-assistant/` is a completely different thing: a **separate Rust
process** that connects over TCP as an ordinary IRC client, nick `assistant`,
and answers with Claude. The C++ server has no idea it is talking to an AI and
contains no code for it — it is outside the 42 build entirely.

```bash
cp .env.example .env      # set ANTHROPIC_API_KEY
docker compose up --build
```

Then in any channel it has joined:

```
!ai what is a channel key for?
assistant: summarise the last few messages
/msg assistant hello
```

It answers only when addressed, and it can *act* — read scrollback, WHO the
roster, read topic and modes, and (when enabled) moderate. Full walkthrough:
[10 — The AI assistant](10-ai-assistant.md).

---

## Checks

* `/msg ircbot !help` returns the five-line help block.
* `!help` in a channel returns nothing.
* `NICK ircbot` is refused.
* On `make mandatory`, `PRIVMSG ircbot :!help` gives `401`.

Guarded by `tests/test_bot.cpp`.

**Next:** [07 — File transfer](07-file-transfer.md)
