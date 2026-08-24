# 05 — Multiple users at once

**Context.** More than two clients, connected simultaneously. This is where a
server that "works" in a single-client demo usually falls over: one client's
slowness must not stall another, and two clients must never end up sharing an
identity.

Everything below runs through **one `epoll_wait` call in one thread**. No fork,
no thread per client, no blocking read. A client that stops reading gets its
own output buffered, not the whole server frozen.

---

## HexChat

Run several instances against the same server. On Linux, one HexChat can hold
several connections to the same network — or run a second profile:

```bash
hexchat &
hexchat --cfgdir=/tmp/hexchat2 &
```

Both join `#general` and talk. What to watch: messages arrive in the same order
everywhere, member lists agree, and one client quitting does not disturb the
other.

---

## netcat — several clients

Each `nc` is one client. In separate terminals:

```bash
nc -C 127.0.0.1 6667      # alice
nc -C 127.0.0.1 6667      # bob
nc -C 127.0.0.1 6667      # carol
```

Or scripted, driving each client through a FIFO held open on its own fd — the
pattern `tests/lib/irc_lib.sh` uses, and the only way to send a command in
pieces:

```sh
mkfifo a.fifo
nc -C 127.0.0.1 6667 < a.fifo > a.out &
exec 3> a.fifo
printf 'PASS mypass\r\n' >&3
printf 'NICK alice\r\n'  >&3
```

---

## Nick collisions

Two connections cannot own the same name, and the check is **case-insensitive**:

```
client C: NICK probeclient      -> registers as probeclie
client D: NICK PROBECLIE        -> :ft_irc 433 * PROBECLIE :Nickname is already in use
```

Note the `*`: D still has no nick, so the server has nothing to address it by.
D picks another and registration continues normally.

The ownership check deliberately counts connections that have sent `NICK` but
have **not finished registering**. Without that, two half-registered clients
could both claim a name and both complete — leaving two live sessions with one
identity.

## Casemapping

`005` advertises `CASEMAPPING=ascii`, and the server honours it everywhere:

```
bob2: PRIVMSG PROBECLIE :case-insensitive delivery
```
```
probeclie receives: :bob2!q@127.0.0.1 PRIVMSG PROBECLIE :case-insensitive delivery
```

`Alice`, `alice` and `ALICE` are one person; `#General` and `#general` are one
room. **ASCII only** — accented letters stay distinct, which is what stops
someone registering a lookalike of an existing nick.

---

## What the server guarantees under concurrency

* **Per-client output queues.** Each client has its own 64 KiB send buffer.
  A client that stops reading fills only its own; everyone else is unaffected.
  Overflow disconnects *that* client, and never mid-broadcast — the others get
  the complete message.
* **Writes only when writable.** The event loop asks for write-readiness only
  while a client has queued output, then drops back to read-only once it
  drains. Nothing ever blocks waiting for a slow peer.
* **Ordering per client.** Everything queued for one client leaves in order.
* **Independent teardown.** One client quitting, timing out, or being killed
  runs its own cleanup — QUIT to its channels, invites retired, memory freed —
  while every other session keeps running.
* **1024 clients** is the cap. Beyond it, new connections are closed.

---

## Try it: a whole populated server, in one command

```bash
scripts/simulation.sh                 # 10 users, several channels, ops, conversation
scripts/simulation.sh --hexchat 2     # two of them as real HexChat windows
scripts/simulation.sh --status
scripts/shutdown_simulation.sh        # free all of it
```

The simulation harness brings up the server and a cast of ten users — some in
several channels, one in none — hands out operator status, and replays a
scripted conversation, all in the background. Clients can be netcat sockets or
real HexChat GUIs, and the same `--send` command drives either. See
[`scripts/sim/README.md`](../../scripts/sim/README.md).

## Try it: 12 clients, one channel

```bash
cd tests && bash ./run_all.sh --only 10
```

`tests/10_stress_multiclient.sh` connects a dozen clients, joins them all to one
channel, has them talk over each other, and checks that every message reaches
every member and that the server is still healthy afterwards.

For the same suite run under two different shells and diffed line by line:

```bash
cd tests && ./run_dual.sh
```

Any surviving difference between the `bash` and `hellish` transcripts is either
a shell bug or genuine server nondeterminism — which is exactly what you want
to know about.

---

## Checks

* Twelve simultaneous clients all register and all receive every broadcast.
* A duplicate nick is refused with `433`, differing only in case.
* Killing one client (`Ctrl+C` on its `nc`) leaves the others fully working.
* `ps -o nlwp= -p $(pgrep ircserv)` stays at **1** — one thread, always.

Guarded by `tests/10_stress_multiclient.sh`, `tests/concurrent_clients.sh`
and `tests/no_forking.sh`.

**Next:** [06 — The bot](06-bot.md)
