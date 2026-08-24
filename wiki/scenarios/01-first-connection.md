# 01 — First connection

**Context.** Nothing is running yet. You want a server up and one client
registered on it. Everything else in this wiki depends on this working.

---

## Starting the server

```bash
./build/bin/ircserv <port> <password>
./build/bin/ircserv 6667 mypass
```

* **port** — 1–65535, strictly parsed. `./build/bin/ircserv 99999999999 x` is rejected
  before a socket is ever created.
* **password** — cannot be empty. This is the *server* password (IRC's `PASS`),
  not a per-user account.

Wrong argument count, bad port, or empty password all exit `1` with a usage
line and no listening socket:

```console
$ ./build/bin/ircserv
usage: ./build/bin/ircserv <port> <password>
$ ./build/bin/ircserv 70000 mypass
port must be a number between 1 and 65535
```

The console then shows a banner and one line per lifecycle event:

```
╔══════════════════════════════════════════════════════════╗
║            ◆ ft_irc - listening on port 6667             ║
╚══════════════════════════════════════════════════════════╝

 ℹ  new connection from 127.0.0.1 (fd 5)
 ✔  registered alice (alice@127.0.0.1)
 ℹ  client disconnected: alice (bye)
```

Stop it with `Ctrl+C` (SIGINT) — it drains, frees every client and channel, and
prints `shutting down — server stopped cleanly`.

> On another machine on your LAN, hand out the address from `hostname -I`
> instead of `127.0.0.1`. The server binds `0.0.0.0`, so it is reachable
> without further configuration.

---

## HexChat

1. **HexChat → Network List** (`Ctrl+S`) → **Add**, name it `ft_irc`.
2. **Edit** → replace the server list with `127.0.0.1/6667`.
3. Put your server password in **Password**, *not* in "Nickserv password".
4. Set **Nick name** — 9 characters max (see below).
5. Uncheck **Use SSL** if it is on. Then **Connect**.

You are registered when the server tab shows the `001`–`005` welcome burst.
To watch the raw protocol while HexChat drives it: **Window → Raw Log**.

---

## netcat

```bash
nc -C 127.0.0.1 6667
```

Then type the three-command handshake:

```
PASS mypass
NICK alice
USER alice 0 * :Alice Liddell
```

The server answers the moment the third one lands:

```
:ft_irc 001 alice :Welcome to the ft_irc Network alice!alice@127.0.0.1
:ft_irc 002 alice :Your host is ft_irc, running version 1.0
:ft_irc 003 alice :This server was created 2025-01-01
:ft_irc 004 alice ft_irc 1.0 o itkol
:ft_irc 005 alice CHANTYPES=# PREFIX=(o)@ CHANMODES=,,kl,it NICKLEN=9 CHANNELLEN=50 TOPICLEN=390 NETWORK=ft_irc CASEMAPPING=ascii :are supported by this server
:ft_irc 422 alice :MOTD File is missing
```

`422` is not an error — this server has no MOTD file and says so, which is what
clients expect at the end of a welcome burst.

**Order does not matter**, arrival does: `NICK` before `PASS` is fine.
Registration completes when all three have been seen *and* the password
matches.

---

## The three things that go wrong

### Wrong password → `464`, then disconnect

```
PASS nope
NICK x
USER x 0 * :x
```
```
:ft_irc 464 x :Password incorrect
```

The connection closes right after. The numeric is guaranteed to reach you
before the close — the server defers the teardown until its output queue has
drained.

### Command before registration → `451`

Only `CAP`, `PASS`, `NICK`, `USER`, `QUIT` and `PONG` run before registration.
Anything else:

```
JOIN #a
```
```
:ft_irc 451 * :You have not registered
```

The `*` is your nick placeholder — you do not have one yet.

### Nickname problems

| You type | You get | Why |
| --- | --- | --- |
| `NICK probeclient` | registered as **`probeclie`** | 9-char cap, **truncated silently**, never rejected |
| `NICK a,b` | `432 :Erroneous nickname` | `,` `space` `*` `?` `!` `@` `.` and a leading digit/`-` are illegal |
| `NICK bob` when bob exists | `433 :Nickname is already in use` | ownership check, ASCII case-insensitive |
| `NICK` alone | `431 :No nickname given` | |

The truncation is the one that costs debugging time: a client registered as
`probeclient` is reachable **only** as `probeclie`. `PRIVMSG probeclient :hi`
gets `401 No such nick/channel`.

### Registering twice → `462`

Once you are in, a second `USER` gets
`:ft_irc 462 bob2 :You may not reregister`. `NICK` keeps working — that is a
nick *change*, not a re-registration.

---

## Checks

* `ss -ltn | grep 6667` shows a listening socket on `0.0.0.0`.
* A registered client sees exactly six lines: `001 002 003 004 005 422`.
* A wrong password produces `464` **and** the transcript ends there.
* `NICK probeclient` → `001 probeclie`.

Guarded by `tests/01_startup.sh`, `tests/02_registration.sh`,
`tests/09_malformed_preauth.sh`, and `ConformanceTest` in
`tests/test_conformance.cpp`.

**Next:** [02 — Channels](02-channels.md)
