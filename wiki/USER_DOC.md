# ft_irc — User Guide

A practical guide to using this IRC server, feature by feature. Each section
explains *what* the feature does, *why* it behaves the way it does, and shows
the fastest way to try it — sometimes with a raw `nc` session (when seeing the
exact protocol lines matters), sometimes with HexChat (when the point is the
user experience).

The server implements a subset of RFC 2812: enough that a real client like
HexChat behaves as it would against any public IRC network, but deliberately
scoped to the project's requirements (no server-to-server, no services layer).

---

## Running the server

```bash
make            # full build (mandatory + bonus + optional extras)
# or
make mandatory  # RFC core only — this is what the grade is decided on
make bonus      # mandatory + Bot + FILE transfer

./build/bin/ircserv <port> <password>
./build/bin/ircserv 6667 test123

## connection ircserv... from one machine to another..
hostname -i ## it should output the result

```

- **port** — the TCP port the server listens on.
- **password** — the connection password every client must send before
  registering. This is the server password (IRC's `PASS`), not a per-user
  account password.

The server is single-threaded and event-driven (one `epoll` loop, non-blocking
sockets). It never spawns a thread or a process per client — every connection
is multiplexed through that one loop.

---

## Two ways to connect

Throughout this guide, examples use whichever client makes the point clearest.

**`nc` (netcat)** shows you the raw protocol — every byte in, every byte out.
Use it when the interesting thing is the exact wire format. Register by typing
the three lines yourself:

```bash
nc -C 127.0.0.1 6667
```
```
PASS test123
NICK alice
USER alice 0 * :Alice Liddell
```

`nc -C` sends CRLF line endings, which IRC requires. Without `-C` some
netcat builds send bare `\n`; this server tolerates that, but real IRC is
CRLF, so `-C` keeps you honest.

**HexChat** builds those lines for you and renders the results as a chat UI.
Use it when the point is what a real user sees. Configure a server at
`127.0.0.1/6667`, set the server password to `test123`, and connect. To watch
the raw protocol underneath HexChat, open **Window → Raw Log**.

> **Why registration is three commands.** `PASS`, `NICK`, and `USER` together
> form the handshake. The server won't treat you as a real user until all three
> arrive and the password matches. Before that, only `CAP`, `PASS`, `NICK`,
> `USER`, `QUIT`, and `PONG` are accepted — anything else gets
> `451 :You have not registered`.

---

## Registration and identity

### Setting your password (PASS)

The password must be correct, or the server rejects you before you can do
anything. A wrong password is reported and then the connection closes:

```
PASS wrongpass
NICK alice
USER alice 0 * :Alice
```
```
:ft_irc 464 alice :Password incorrect
```

The `464` reaches you *before* the socket closes — you get told why, rather
than seeing a silent drop. That matters with a real client: HexChat shows you
"Password incorrect" instead of an unexplained disconnect.

### Choosing a nickname (NICK)

Your nickname is your identity on the network. It must be unique
(case-insensitively — `Alice` and `alice` are the same nick) and at most 9
characters.

```
NICK alice
```

**Over-long nicks are rejected, not truncated.** A nickname may be at most 9
characters. Send a longer one and the server answers `432
ERR_ERRONEUSNICKNAME` and leaves your nick alone — it will not quietly
register you under a shortened name you did not choose.

During registration a refused nick means registration does not complete. Send
a nick that fits and carry on:

```
NICK abcdefghijklmno
USER x 0 * :X
```
```
:ft_irc 432 * abcdefghijklmno :Erroneous nickname
```

After registration the refusal changes nothing: no `NICK` line is echoed to
you, nothing is broadcast to your channels, and you keep the nick you already
had.

```
NICK qwertyuiopasd
```
```
:ft_irc 432 short qwertyuiopasd :Erroneous nickname
```

The limit is advertised in the `005` burst as `NICKLEN=9`, so a client can
read it rather than guess.

> **Why reject instead of truncate?** This server used to truncate, and the
> change is deliberate. RFC 2812 §3.1.2 answers `432`, and the grammar's own
> `nickname` production — `( letter / special ) *8( letter / digit / special
> / "-" )` — allows 9 characters and no more. Truncating meant registering
> someone under a name they never asked for, which then shows up in every
> prefix they send.
>
> **The cost is real.** A client whose collision retry *appends* to the nick —
> HexChat's `_`, `_1` — only makes an over-long nick longer, so it cannot
> recover from a `432` on its own. If you use such a client, choose a nick of
> 9 characters or fewer to begin with.

Invalid *characters* are still rejected (this is a different problem from
length):

```
NICK ab#cd
```
```
:ft_irc 432 bob ab#cd :Erroneous nickname
```

A nick that's already taken gets `433`:

```
NICK alice
```
```
:ft_irc 433 * alice :Nickname is already in use
```

### Completing registration (USER)

```
USER alice 0 * :Alice Liddell
```

The last parameter (after the colon) is your "real name" and may contain
spaces; the earlier fields are mostly legacy. Once `USER` completes a valid
`PASS`+`NICK`+`USER` set, the server sends the welcome burst (`001`–`005`) and
you're registered.

---

## Channels

### Joining (JOIN)

```
JOIN #general
```

Joining a channel that doesn't exist creates it, and the creator becomes its
operator. You'll see the topic (or that there is none), the member list, and
the channel modes:

```
:alice!alice@127.0.0.1 JOIN #general
:ft_irc 331 alice #general :No topic is set
:ft_irc 353 alice = #general :@alice
:ft_irc 366 alice #general :End of /NAMES list
:ft_irc 324 alice #general +
:ft_irc 329 alice #general 1785598392
```

The `@` before your nick in the `353` list means you're a channel operator. The
`324` line lists the channel's current modes and `329` is its creation
timestamp.

In HexChat, `/join #general` does the same and opens a channel tab.

### Talking (PRIVMSG and NOTICE)

`PRIVMSG` sends a message — to a channel or to another user:

```
PRIVMSG #general :hello everyone
PRIVMSG bob :hey, got a minute?
```

> **The space before the colon is mandatory.** The colon marks the start of the
> "trailing" parameter (the message text, which may contain spaces). From
> HexChat you never see this — it builds the line for you. From `nc`, forgetting
> the space changes the meaning:
> ```
> PRIVMSG bob:hello   -> 412 :No text to send   (no space -> no text parameter)
> PRIVMSG #bob :hi    -> 403 :No such channel    (the # makes it a channel)
> ```

`NOTICE` is identical in form but signals "don't auto-reply to this" — it's for
automated messages. Clients won't bounce error replies off a `NOTICE`, which is
why bots and servers use it.

Messaging errors are specific: a message to a nick that doesn't exist gets
`401 :No such nick/channel`; to a channel you're not in, `404 :Cannot send to
channel`; with no text at all, `412`.

### Leaving (PART) and topics (TOPIC)

```
PART #general :goodbye
TOPIC #general :Project planning — read the pinned notes
```

`TOPIC` with no text *reads* the current topic; with text, it *sets* it. In a
`+t` channel (see modes below), only operators can set it.

---

## Channel operators and modes (MODE)

Channel operators control the channel. The creator starts as operator;
operators can grant the status to others.

```
MODE #general +o bob        # make bob an operator
MODE #general -o bob        # remove it
```

Every accepted `MODE` change is echoed back to you (and to the rest of the
channel) as a line prefixed with the setter:

```
:alice!alice@127.0.0.1 MODE #general +k secret
```

The mandatory channel modes are `+i`, `+t`, `+k`, `+o`, `+l`:

| Mode | Effect | Example |
|------|--------|---------|
| `+i` | Invite-only: nobody joins without an invite | `MODE #general +i` |
| `+t` | Only operators can change the topic | `MODE #general +t` |
| `+k` | Sets a channel key (password) | `MODE #general +k secret` |
| `+o` | Grants/removes operator status | `MODE #general +o bob` |
| `+l` | Limits the number of members | `MODE #general +l 20` |

> **Why these five.** They're the RFC channel modes the project requires — the
> ones a moderator actually needs: control who joins (`+i`, `+k`, `+l`), who
> can change the topic (`+t`), and who has authority (`+o`).

### Invite-only in practice (+i and INVITE)

```
MODE #general +i
```

Now an uninvited user is refused:

```
JOIN #general
```
```
:ft_irc 473 bob #general :Cannot join channel (+i)
```

An operator invites them, and then they can join:

```
INVITE bob #general
```
Only members of the channel it grants access to can send an `INVITE`, and only
operators may do so in a `+i` channel — a non-operator trying to invite gets
`482 :You're not channel operator`.

### Keyed channels (+k)

```
MODE #general +k secret
```

Now joining requires the key:

```
JOIN #general
```
```
:ft_irc 475 bob #general :Cannot join channel (+k)
```
```
JOIN #general secret
```
succeeds. A wrong key gets the same `475`.

### Kicking (KICK)

```
KICK #general troublemaker :please read the rules
```

Only operators can kick. The kicked user is removed and everyone in the channel
sees it.

---

## Keeping the connection alive (PING/PONG)

The server periodically sends a `PING` to check you're still there; your client
must answer with a matching `PONG`. HexChat does this automatically. From `nc`
you'd answer by hand:

```
PING :ft_irc
```
```
PONG :ft_irc
```

If a client stops answering, the server eventually drops it with a
`Ping timeout`. Conversely, the server answers *your* `PING`:

```
PING :keepalive
```
```
:ft_irc PONG ft_irc :keepalive
```

> **Why this exists.** TCP can keep a socket "open" long after the peer is
> actually gone (a frozen machine, a pulled cable with no RST). PING/PONG is how
> the server detects and reclaims those dead connections instead of leaking
> them.

---

## Querying (WHO, WHOIS, USERHOST)

```
WHO #general        # who's in the channel
WHOIS bob           # details about a user
USERHOST bob        # bob's user@host
```

HexChat issues these under the hood when you open a channel or hover a nick;
from `nc` you can run them directly to see the `352`/`315` (WHO) and
`311`/`318` (WHOIS) replies.

---

## Quitting (QUIT)

```
QUIT :heading out
```

The server tells everyone who shared a channel with you that you've left —
once, no matter how many channels you had in common — and cleans you out of all
of them. Closing the socket abruptly (Ctrl+C in `nc`) is handled the same way,
with a `Connection closed` reason.

---

# Bonus features

These require `make bonus`. Confirm you're running the bonus build before
testing them — a `!help` to the bot that comes back `401 :No such
nick/channel` means you're on the mandatory binary.

## The Bot (ircbot)

`ircbot` is a **virtual user inside the server** — not a separate process, not a
connected client. It only reacts to private messages addressed to its nick.
Typing `!help` into a channel does nothing; it's just channel text.

```
/msg ircbot !help
```
(from `nc`: `PRIVMSG ircbot :!help`)

Commands:

```
/msg ircbot !time          # current server time
/msg ircbot !info          # server info
/msg ircbot !info #general  # that channel's member count and modes
/msg ircbot !joke          # a joke (rotates, doesn't repeat one string)
```

An unknown command gets a helpful reply rather than silence:

```
/msg ircbot !nonsense
```
```
:ircbot PRIVMSG you :Unknown command. Type !help for available commands.
```

The bot's nick is reserved — you can't take it (`433`), so nobody can
impersonate it.

## FILE transfer

A server-mediated file transfer over a small `FILE` protocol. The server
relays base64-encoded chunks between two clients; **it never decodes the
payload and never touches disk** — it's pure relay. The proof of correctness is
that what the receiver gets is byte-for-byte what the sender sent.

The happy path, with `hello world!` (12 bytes, base64 `aGVsbG8gd29ybGQh`):

```
# alice (sender)          # bob (receiver, already registered)
FILE SEND bob hello.txt 12
    -> alice: NOTICE :FILE 1 offered to bob
    -> bob:   FILE OFFER 1 hello.txt 12
                            FILE ACCEPT 1
    -> alice: FILE OK 1
FILE DATA 1 aGVsbG8gd29ybGQh
    -> bob:   FILE DATA 1 aGVsbG8gd29ybGQh
FILE END 1
    -> bob:   FILE END 1 12
```

Decode what bob received to confirm it survived intact — in a *separate shell*,
not inside `nc` (anything typed into `nc` goes to the server as an IRC line):

```bash
echo 'aGVsbG8gd29ybGQh' | base64 -d      # -> hello world!
```

The transfer id (`1` above) increments per offer — use whatever id the server
actually gave you.

**Rejection is reported, not silent.** If bob sends `FILE REJECT 1`, alice gets
`FILE NO 1`. And data sent after a rejection goes nowhere: alice sending
`FILE DATA` for a rejected/unknown id gets `NOTICE :FILE: no transfer with id
N`, and bob receives nothing. A transfer to a nonexistent user is refused up
front: `NOTICE :FILE: no such nick <name>`.

> **Why FILE errors are NOTICEs, not numerics.** `FILE` isn't an RFC command, so
> no standard numeric applies to it. The server reports its errors as
> `NOTICE`s instead.

## DCC passthrough

Separate from the `FILE` protocol above. HexChat's own file-transfer uses DCC,
which rides inside a CTCP-wrapped `PRIVMSG` (the payload is bracketed by `\x01`
bytes). The server implements **no DCC logic** — it just relays that
`\x01`-wrapped payload untouched, which is all HexChat needs to pop up its
transfer dialog.

From HexChat this "just works" (initiate a DCC send between two connected
clients). The key property is that the server passes the `\x01` control bytes
through intact rather than stripping them — if it stripped them, HexChat's DCC
would be dead.

---

## A note for graders / defense

Every feature above has been verified against HexChat, with Libera Chat
(running Solanum) as the reference for "how a real server behaves." The design
goal throughout: using HexChat against this server should feel like using it
against any official IRC network, within the mandatory + bonus feature set.

A few deliberate, documented divergences from Solanum remain — all cosmetic,
none affecting a HexChat session (e.g. minor differences in a couple of
numeric-reply strings). They're catalogued in the project's testing notes.