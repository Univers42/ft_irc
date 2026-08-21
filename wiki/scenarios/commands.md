# Command reference

Every command this server implements, its raw wire syntax, the HexChat
equivalent, and what it can answer with. Anything not listed here answers
`421 :Unknown command`.

**Raw syntax rule.** The space before `:` is mandatory — the colon opens the
*trailing* parameter, the only one allowed to contain spaces. `PRIVMSG bob :hi`
works; `PRIVMSG bob:hi` does not.

---

## Registration

| Raw | HexChat | Notes |
| --- | --- | --- |
| `PASS <password>` | set in Network List | Server password. Must come before registration completes |
| `NICK <nick>` | `/nick <nick>` | ≤ 9 chars, **truncated** past that. Also changes nick later |
| `USER <user> 0 * :<realname>` | automatic | Completes registration |
| `QUIT [:reason]` | `/quit [reason]` | Broadcasts to your channels |
| `CAP LS` / `CAP END` | automatic | Accepted and closed out; no capabilities negotiated |

Errors: `431` no nick given · `432` erroneous nick · `433` nick in use ·
`451` not registered · `461` need more params · `462` may not reregister ·
`464` password incorrect.

**Only** `CAP` `PASS` `NICK` `USER` `QUIT` `PONG` run before registration.

## Channels

| Raw | HexChat | Notes |
| --- | --- | --- |
| `JOIN <#chan>[,<#chan>] [key[,key]]` | `/join #chan [key]` | Creates it if new; creator becomes operator. Keys are positional |
| `PART <#chan>[,<#chan>] [:reason]` | `/part [reason]` | Last member out destroys the channel |
| `TOPIC <#chan> [:text]` | `/topic [text]` | No text = read it. 390 chars, truncated. `+t` restricts setting to operators |

Errors: `403` no such channel · `442` not on that channel · `461` ·
`471` `+l` full · `473` `+i` · `475` `+k` · `476` bad channel mask · `482`.

## Messaging

| Raw | HexChat | Notes |
| --- | --- | --- |
| `PRIVMSG <target>[,<target>] :<text>` | type in the box, or `/msg <nick> <text>` | Target is a nick or `#channel`. You never receive your own |
| `NOTICE <target> :<text>` | `/notice <nick> <text>` | Same routing, **never auto-replies with an error** |
| `PING [:token]` | automatic | Server answers `PONG` |
| `PONG [:token]` | automatic | Answer the server's keepalive |

Errors: `401` no such nick · `404` cannot send to channel (you are not in it) ·
`411` no recipient · `412` no text to send.

## Operator

| Raw | HexChat | Needs |
| --- | --- | --- |
| `KICK <#chan> <nick> [:reason]` | `/kick <nick> [reason]` | operator |
| `INVITE <nick> <#chan>` | `/invite <nick> #chan` | membership; operator if `+i` |
| `MODE <#chan> <flags> [params]` | `/mode #chan +x` | operator |
| `MODE <#chan>` | `/mode #chan` | **membership** — the reply carries the `+k` key |

Errors: `441` they aren't on that channel · `443` already on channel ·
`472` unknown mode char · `482` not channel operator ·
`525` invalid key · `696` invalid mode parameter.

### Channel modes

| Mode | Param | Effect |
| --- | --- | --- |
| `+i` | — | Invite-only |
| `+t` | — | Topic restricted to operators |
| `+k` | key on set | Channel password, 1–23 chars, no space/comma/control |
| `+o` | nick | Grant / revoke operator |
| `+l` | number on set | Member cap, 1–65535 |

Flags combine (`MODE #c +itk-o secret bob`) and are applied **one at a time** —
a rejected flag does not roll back the valid ones next to it.

## Queries

| Raw | HexChat | Returns |
| --- | --- | --- |
| `WHO <#chan\|nick>` | `/who #chan` | `352` per user, then `315` |
| `WHOIS <nick>` | `/whois nick` | `311` `312` `319` then `318` |
| `USERHOST <nick> [...]` | `/userhost nick` | `302`, up to 5 nicks |

## Bonus

| Raw | Tier | See |
| --- | --- | --- |
| `PRIVMSG ircbot :!help\|!time\|!info\|!joke` | bonus | [06 — The bot](06-bot.md) |
| `FILE SEND\|ACCEPT\|REJECT\|DATA\|END\|ABORT` | bonus | [07 — File transfer](07-file-transfer.md) |

`FILE` errors come back as `NOTICE`, not numerics — it is not an RFC command.

---

## Numerics this server sends

**Welcome** — `001` welcome · `002` your host · `003` created · `004` my info ·
`005` isupport · `422` no MOTD

**Replies** — `221` umode · `302` userhost · `311` `312` `318` `319` whois ·
`315` `352` who · `324` channel modes · `329` creation time ·
`331` `332` `333` topic · `341` inviting · `353` `366` names

**Errors** — `401` no such nick · `403` no such channel ·
`404` cannot send to channel · `411` no recipient · `412` no text ·
`421` unknown command · `431` no nick given · `432` erroneous nick ·
`433` nick in use · `441` not in channel · `442` not on channel ·
`443` already on channel · `451` not registered · `461` need more params ·
`462` may not reregister · `464` password mismatch · `471` channel full ·
`472` unknown mode · `473` invite only · `475` bad key · `476` bad mask ·
`482` not operator · `502` users don't match · `525` invalid key ·
`696` invalid mode param

`Replies.hpp` is an inventory of what this server **actually sends** — numerics
for unimplemented commands (LIST, MOTD, AWAY, OPER…) were deliberately deleted.
Do not re-add one before the code that sends it.

## Limits

| Thing | Limit | On excess |
| --- | --- | --- |
| Nickname | 9 | **Truncated silently** |
| Channel name | `#` + 2–50 | `476` |
| Channel key | 1–23 | `525` |
| Member limit | 1–65535 | `696` |
| Topic | 390 | Truncated |
| Line | 512 bytes incl. CRLF | Truncated, remainder discarded |
| Send queue | 64 KiB / client | That client disconnected |
| Clients | 1024 | Connection closed |
| Ping | 120 s idle → PING, +120 s → drop | `Ping timeout` |

Nicks and channels are **ASCII case-insensitive** (`CASEMAPPING=ascii`).
Accented characters stay distinct.

---

**Back to:** [scenario index](README.md)
