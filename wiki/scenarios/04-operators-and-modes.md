# 04 — Operators & modes

**Context.** Alice created `#ops`, so she is its operator. Bob joins as a
regular user. This is the scenario the evaluation sheet spends the most time
on: *an operator can moderate, a regular user cannot*, and every mode does what
it claims.

The transcripts below are one continuous captured session — Alice's view on the
left of each pair, Bob's on the right.

---

## The setup

```
alice: JOIN #ops        -> creates it, becomes operator
bob:   JOIN #ops        -> joins as a regular user
```

Bob's names list makes the asymmetry visible immediately:

```
:ft_irc 353 bob = #ops :@alice bob
```

`@alice` is an operator, `bob` is not.

---

## A regular user is refused

Bob tries to moderate:

```
bob: MODE #ops +t
```
```
:ft_irc 482 bob #ops :You're not channel operator
```

Alice does the same thing and it works — every member sees it:

```
alice: MODE #ops +t
```
```
:alice!alice@127.0.0.1 MODE #ops +t
```

Bob's `TOPIC #ops :hi` now also gets `482`, because `+t` restricts the topic to
operators. **Both users see nothing change** — a refused mode is refused
completely, not partially applied.

---

## Granting and revoking operator (`+o` / `-o`)

```
alice: MODE #ops +o bob      -> :alice!alice@127.0.0.1 MODE #ops +o bob
alice: MODE #ops -o bob      -> :alice!alice@127.0.0.1 MODE #ops -o bob
```

Broadcast to the whole channel, so every member's list updates. Operator status
is **per channel** — being `@` in `#ops` grants nothing in `#dev`.

---

## Invite-only (`+i` + `INVITE`)

```
alice: MODE #ops +i
alice: KICK #ops bob :read the rules
```

Bob is removed, and now cannot get back in:

```
bob: JOIN #ops
```
```
:ft_irc 473 bob #ops :Cannot join channel (+i)
```

Alice invites him:

```
alice: INVITE bob #ops
```

```
alice sees: :ft_irc 341 alice bob #ops
bob sees:   :alice!alice@127.0.0.1 INVITE bob :#ops
```

Bob's next `JOIN #ops` succeeds and the invite is consumed.

> **Invites are keyed to the connection, not the nickname.** A nick is a label
> a client can drop with `NICK` or free with `QUIT` — if invites were stored by
> name, the next person to claim `bob` would inherit his way into a `+i`
> channel. Quitting also retires your pending invites from *every* channel,
> including ones you never joined, which is exactly where an unredeemed invite
> would otherwise hide.

You must be **in** a channel to invite to it, and in a `+i` channel only
operators may.

---

## Keys (`+k`)

```
alice: MODE #ops +k s3cret       -> :alice!… MODE #ops +k s3cret
```

Outsiders now get `475 :Cannot join channel (+k)`; the way in is
`JOIN #ops s3cret`. Remove with `MODE #ops -k`.

Keys are 1–23 characters with no space, comma or control character. A bad one
is rejected with `525 :Key is not well-formed` — the channel keeps its old key
rather than ending up in a half-set state.

> **Reading modes needs membership too.** `MODE #ops` with no flags answers
> `324`, and that reply carries the `+k` key as a parameter. Letting a
> non-member read it would hand out the channel password and defeat `+k`
> entirely, so a stranger gets `442 :You're not on that channel`.

## Member limit (`+l`)

```
alice: MODE #ops +l 2
```

The third person to try gets `471 :Cannot join channel (+l)`. Remove with
`MODE #ops -l`. Valid range 1–65535 — anything outside it is rejected:

```
alice: MODE #ops -o+l bob 99999
```
```
:ft_irc 696 alice #ops l 99999 :Invalid channel limit
:alice!alice@127.0.0.1 MODE #ops -o bob
```

Note what happened there: the `-o` **applied** and the `+l` was **rejected**.
Modes in a combined string are evaluated one flag at a time, so a bad parameter
never rolls back the valid flags beside it.

---

## Reading the state back

```
alice: MODE #ops
```
```
:ft_irc 324 alice #ops +it
:ft_irc 329 alice #ops 1787316336
```

Modes taking a parameter list it after the flags — `+itkl s3cret 2`.

---

## KICK

```
alice: KICK #ops bob :read the rules
```
```
:alice!alice@127.0.0.1 KICK #ops bob :read the rules
```

Everyone sees it, including bob, and the reason is optional. Operator-only
(`482` otherwise); kicking someone who is not in the channel gives
`441 :They aren't on that channel`.

---

## Mode reference

| Mode | Parameter | Effect | Refused with |
| --- | --- | --- | --- |
| `+i` / `-i` | — | Invite-only | `473` on join |
| `+t` / `-t` | — | Topic restricted to operators | `482` on TOPIC |
| `+k` / `-k` | key on set | Channel password, 1–23 chars | `475` on join, `525` if malformed |
| `+o` / `-o` | nick | Grant / revoke operator | `441` if not a member |
| `+l` / `-l` | number on set | Member cap, 1–65535 | `471` on join, `696` if out of range |

Everything above needs operator status; a non-operator attempt is always
`482 :You're not channel operator`. An unrecognised flag gives
`472 :is unknown mode char to me`.

---

## Checks

* Bob's `MODE`/`TOPIC`/`KICK`/`INVITE` attempts all return `482` and change nothing.
* Alice's succeed and are **broadcast to every member**, not just to her.
* `+i` blocks with `473`, an `INVITE` clears it exactly once.
* `+k` blocks with `475`; `JOIN #ops s3cret` gets through.
* `+l 2` blocks the third joiner with `471`.
* A non-member's `MODE #ops` gets `442`, never the key.
* `MODE #ops -o+l bob 99999` applies `-o` and rejects `+l` with `696`.

Guarded by `tests/07_kick_invite_topic.sh`, `tests/08_modes.sh`, and
`tests/test_security.cpp`.

**Next:** [05 — Multiple users](05-multiple-users.md)
