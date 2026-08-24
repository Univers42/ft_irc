# 08 — Failure & resilience

**Context.** Everything above assumed things go right. This page is the
opposite: split packets, clients killed mid-sentence, peers that stop reading,
floods, and shutdown. These are the cases an evaluator will actually try, and
the rule for all of them is the same — **the server must never crash, never
hang, and never leak.**

---

## Split TCP packets — the subject's own test

TCP does not preserve message boundaries. A client can send `command` as
`com`, `man`, `d\r\n` in three packets seconds apart, and the server must
reassemble it.

```bash
nc -C 127.0.0.1 6667
```

Register, then type `JO`, wait, `IN #fr`, wait, `ag` then Enter:

```
:f!f@127.0.0.1 JOIN #frag
:ft_irc 331 f #frag :No topic is set
:ft_irc 353 f = #frag :@f
:ft_irc 366 f #frag :End of /NAMES list
```

The join fired exactly once, when the terminating newline arrived. The reverse
also holds: several commands in **one** packet are executed in order, all of
them.

Guarded by `tests/03_tcp_framing.sh`, `tests/partial_input.sh`,
`tests/multiple_cmd_in_one_burst.sh`.

---

## Abrupt disconnect

Kill a client without a `QUIT` — `Ctrl+C` on its `nc`, or:

```bash
kill -9 $(pgrep -f 'nc -C 127.0.0.1 6667' | head -1)
```

The server notices the closed socket, broadcasts a QUIT to the channels that
client was in, frees it, and keeps running. Its channel peers see it leave;
nobody else is disturbed. A connection reset (RST) is handled the same way.

Guarded by `tests/04_disconnect.sh`, `RobustnessTest.AbruptDisconnectViaRST`.

---

## Ping timeout

The server pings an idle client after **120 seconds** and drops it 120 seconds
later if nothing answers. Real clients handle this invisibly. From `nc` you
answer by hand:

```
:ft_irc PING :ft_irc        <- server
PONG :ft_irc                <- you
```

You can also ping the server yourself:

```
PING :hello
```
```
:ft_irc PONG ft_irc :hello
```

Sit on an `nc` session for four minutes without typing and you will be
disconnected with `Ping timeout` — that is the keepalive working, not a bug.

---

## A client that stops reading

Freeze a client without closing it — `Ctrl+Z` on its `nc`, or `kill -STOP`:

```bash
kill -STOP $(pgrep -f 'nc -C 127.0.0.1 6667' | head -1)
```

Then flood the channel it is in from another client. What happens:

1. The frozen client's output queue fills. **Nobody else is affected** — the
   server never blocks on a `send()`.
2. At **64 KiB** queued, that client alone is disconnected.
3. Every other member received every message, complete.

The overflow never lands mid-broadcast: a client is dropped at a safe sweep
point, so no other member gets half a line.

Guarded by the frozen-reader tests in `tests/test_robustness.cpp` and by
`tests/10_stress_multiclient.sh`, which drives a non-reading client.

---

## Flooding

Throw traffic at it:

```bash
yes 'PRIVMSG #general :flood' | head -20000 | nc -C 127.0.0.1 6667
```

Bounded queues absorb it; a client that outruns its own queue is disconnected;
the server stays up. An over-long line is truncated at 512 bytes and its
remainder discarded **through** its terminator — padding cannot smuggle a
second command past the limit.

Malformed input at every stage is answered, never crashed on: unknown commands
give `421`, missing parameters `461`, garbage before registration `451`.

Guarded by `tests/09_malformed_preauth.sh`.

---

## Shutdown

`Ctrl+C` (SIGINT) or `kill -TERM`. The loop exits, every client and channel is
destroyed, every extension is freed, and the process returns `0`:

```
shutting down — server stopped cleanly
```

This is checked **with clients still connected** — the interesting case is
tearing down live sessions, not an empty server.

---

## Proving there is no leak

```bash
bash scripts/memcheck.sh --auto
```

Drives four client sessions under Valgrind, confirms one of them really joined
a channel (by matching a real `RPL_WHOREPLY` member line, not a bare nickname),
then sends SIGTERM with two clients still live. Three exit codes:

| Code | Meaning |
| --- | --- |
| `0` | Clean — no leaks, and the scenario genuinely ran |
| `97` | Valgrind found a leak |
| `90` | Setup was not verified — the scenario never happened |

The `90` exists because a "no leak" pass is worthless if no client ever joined
anything. A broken `JOIN` once passed a green gate that way.

Manual equivalent:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./build/bin/ircserv 6667 mypass
# connect, join, talk, then Ctrl+C
```

---

## Checks

* A command split across three packets executes once, on the newline.
* Several commands in one packet all execute, in order.
* `kill -9` on a client leaves the server and every other client healthy.
* A `kill -STOP`'d client is dropped alone; others lose nothing.
* SIGINT with clients connected exits `0` with no leaks.
* `ps -o nlwp= -p $(pgrep ircserv)` is `1` throughout.

**Next:** [09 — Platform extras](09-platform-extras.md)
