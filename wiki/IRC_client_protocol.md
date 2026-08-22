# IRC client protocol — RFC 2812 notes

`FT_IRC_CLIENT_PROTOCOL/` holds the extracts of
[RFC 2812](https://datatracker.ietf.org/doc/html/rfc2812) that this project is
actually built against — the paragraphs that decide behaviour, kept verbatim so
there is no paraphrase to drift.

This page is the index, and it says for each one **what the server does about
it** and **where that is verified**.

| Note | RFC | What it governs | Implemented in | Verified by |
| --- | --- | --- | --- | --- |
| [messages](FT_IRC_CLIENT_PROTOCOL/messages.md) | §2.3 | Prefix / command / params, the 512-octet limit, max 15 params | `src/Message.cpp`, `src/Client.cpp:104` | [D3](RFC-CONFORMANCE.md#d3--more-than-15-parameters-are-parsed), framing tables |
| [protocol_grammar_rules](FT_IRC_CLIENT_PROTOCOL/protocol_grammar_rules.md) | §2.3.1 | The full ABNF | `src/Message.cpp:7` | `verify_grammar.sh` |
| [connection_registration](FT_IRC_CLIENT_PROTOCOL/connection_registration.md) | §3.1 | PASS before NICK/USER, RPL_WELCOME | `src/CommandRegistration.cpp` | `tests/02_registration.sh` |
| [password_manage](FT_IRC_CLIENT_PROTOCOL/password_manage.md) | §3.1.1 | PASS, `461`, `462` | `cmdPass` | `tests/09_malformed_preauth.sh` |
| [nick_message](FT_IRC_CLIENT_PROTOCOL/nick_message.md) | §3.1.2 | NICK, `431` `432` `433` | `cmdNick` | `verify_names.sh` |
| [user_message](FT_IRC_CLIENT_PROTOCOL/user_message.md) | §3.1.3 | USER, `<mode>` and `<unused>` | `cmdUser` | `tests/02_registration.sh` |
| [users](FT_IRC_CLIENT_PROTOCOL/users.md) | §1.2.1 | Nicknames are ≤ 9 characters | `MAX_NICKLEN` | `verify_names.sh` |
| [channel_names](FT_IRC_CLIENT_PROTOCOL/channel_names.md) | §1.3 | `#` prefix, ≤ 50 chars, no space/BEL/comma, case-insensitive | `Server::isValidChannelName` | `verify_names.sh` |
| [operators](FT_IRC_CLIENT_PROTOCOL/operators.md) | §1.2.2 | What an operator is | `src/CommandOperator.cpp` | `tests/07`, `tests/08` |

---

## Where this server departs from the notes

Five places, all deliberate, all measured — the detail and the reasoning are in
[**RFC-CONFORMANCE.md**](RFC-CONFORMANCE.md):

| | Divergence | Direction |
| --- | --- | --- |
| D1 | `` ` `` (%x60) is a legal nickname character; refused with `432` | stricter |
| D2 | A bare LF is accepted as a line terminator | looser |
| D3 | More than 15 parameters are parsed | looser |
| D4 | `:` is kept inside a channel name instead of splitting off a mask | looser |
| D5 | 8-bit octets are accepted in a channel key | looser |

---

## Scope — what this project implements of RFC 2812

The subject narrows the RFC deliberately. **In scope:** `PASS` `NICK` `USER`
`QUIT` `CAP` · `JOIN` `PART` `TOPIC` · `PRIVMSG` `NOTICE` `PING` `PONG` ·
`KICK` `INVITE` `MODE` (`i t k o l`) · `WHO` `WHOIS` `USERHOST`.

**Out of scope, and not implemented:** server-to-server (the subject forbids
it — which is also why `channel`'s mask half in D4 has nothing to consume),
services, `OPER` and network operators, `LIST` `MOTD` `AWAY` `INFO` `LUSERS`,
user modes, and `!`/`&`/`+` channel prefixes (`005` advertises `CHANTYPES=#`).

`include/Replies.hpp` is an inventory of what the server *actually sends* —
numerics for unimplemented commands were deleted rather than left as
aspirations. Don't re-add one before the code that sends it.

Two notes in this set describe things this project does **not** do, and are
kept for context only: `operators.md` is about *network* operators (`KILL`,
`SQUIT`), not the channel operators the subject asks for; and
`user_message.md`'s `<mode>` bitmask is parsed and ignored, since user modes
are out of scope.

---

## See also

* [RFC-CONFORMANCE.md](RFC-CONFORMANCE.md) — the measured conformance report
* [scenarios/commands.md](scenarios/commands.md) — every command and its numerics
* [DOCUMENTATION.md](DOCUMENTATION.md) — how the server is built
* [RFC 2812](https://datatracker.ietf.org/doc/html/rfc2812) ·
  [RFC 1459](https://datatracker.ietf.org/doc/html/rfc1459) ·
  [Modern IRC](https://modern.ircdocs.horse/)
