# 02 — Channels

**Context.** You are registered. Now you want a room: create one, see who is in
it, set a subject, and leave. Channels are created on demand by the first
person to `JOIN` — there is no "create channel" command, and **the creator
becomes its operator**.

---

## HexChat

* `/join #general` — or **Server → Join a Channel**.
* The member list on the right shows `@alice` — the `@` is the operator prefix
  advertised in `005` as `PREFIX=(o)@`.
* Double-click the channel tab title to set the topic, or `/topic <text>`.
* `/part` leaves the current channel; closing the tab does the same.

---

## netcat — the full join

```
JOIN #general
```

```
:alice!alice@127.0.0.1 JOIN #general
:ft_irc 331 alice #general :No topic is set
:ft_irc 353 alice = #general :@alice
:ft_irc 366 alice #general :End of /NAMES list
:ft_irc 324 alice #general +
:ft_irc 329 alice #general 1787316319
```

Six lines, and each one has a job:

| Line | Meaning |
| --- | --- |
| `JOIN` echo | Your own join, relayed with your full prefix `nick!user@host` — this is how *every* member learns you arrived, you included |
| `331` / `332`+`333` | No topic / the topic plus who set it and when |
| `353` | The names list. `=` means public; `@` marks operators |
| `366` | End of names — clients use it to know the list is complete |
| `324` | Current modes. `+` alone means none set |
| `329` | Channel creation time |

`324`/`329` are sent unprompted so a client can render the room's state without
having to ask.

**Names lists are chunked.** A crowded channel is split across several `353`
lines rather than truncated — the 512-byte RFC line limit is a hard cap and
dropping members would silently desync every client's member list.

---

## Topics

```
TOPIC #general :Project planning       set it
TOPIC #general                         read it
```

Setting broadcasts to everyone in the room; reading answers you alone:

```
:alice!alice@127.0.0.1 TOPIC #general :Project planning
:ft_irc 332 alice #general :Project planning
:ft_irc 333 alice #general alice 1787316420
```

Topics are capped at 390 characters and **truncated**, not rejected. When the
channel is `+t`, only operators may set one — see
[04 — Operators & modes](04-operators-and-modes.md).

---

## Joining several at once

```
JOIN #dev,#ops,#random
JOIN #locked,#open secret
```

Comma-separated, processed left to right, each one succeeding or failing on its
own. Keys are **positional**: in `JOIN #a,#b,#c k1,,k3`, the empty middle field
means `#b` has no key — it does not shift `k3` onto `#b`.

## Leaving

```
PART #general :later
```
```
:alice!alice@127.0.0.1 PART #general :later
```

Everyone still in the room sees it. When the **last** member parts, the channel
is destroyed — modes, topic, operator list and all. Re-joining `#general` a
second later gives you a brand-new empty channel that you own:

```
PART #t :later
MODE #t
```
```
:y!y@127.0.0.1 PART #t :later
:ft_irc 403 y #t :No such channel
```

That `403` is correct: between the two lines, the channel ceased to exist.

---

## Invalid names

```
JOIN badname
```
```
:ft_irc 476 n1 badname :Bad Channel Mask
```

A channel name must start with `#`, be 2–50 characters, and contain no space,
comma, or control character.

Names are **case-insensitive**: `#General` and `#general` are one room. The
spelling used by whoever created it is what everyone sees — the server echoes
its own stored form, never the caller's, so nobody's channel list desyncs.

---

## Checks

* First joiner appears as `@alice` in `353`; the second as plain `bob`.
* `366` always follows the `353`s.
* Parting the last member then `MODE #chan` gives `403`.
* `JOIN #a,#b` produces two complete join bursts.

Guarded by `tests/06_channel_join_part.sh` and `tests/test_channel.cpp`.

**Next:** [03 — Messaging & queries](03-messaging.md)
