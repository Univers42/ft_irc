# Server-side logging & protocol trace

The console can show the entire conversation between the server and its
clients — every line, in both directions, in the exact RFC 2812 syntax it
travels in.

```bash
FT_IRC_LOG=trace ./ircserv 6667 mypass
```

```
18:40:17  fd   6  <<  bob        JOIN #room                                    [JOIN]
18:40:17  fd   5  >>  alice      :bob!bob@127.0.0.1 JOIN #room                 [JOIN]
18:40:17  fd   6  >>  bob        :bob!bob@127.0.0.1 JOIN #room                 [JOIN]
18:40:17  fd   6  >>  bob        :ft_irc 353 bob = #room :@alice bob           [353 RPL_NAMREPLY]
18:40:17  fd   5  <<  alice      PRIVMSG #room :hey bob                        [PRIVMSG]
18:40:17  fd   6  >>  bob        :alice!alice@127.0.0.1 PRIVMSG #room :hey bob [PRIVMSG]
```

Read the third and fourth lines together: bob's `JOIN` arrives on **fd 6**, and
the server fans it out to **fd 5** as well — that pair *is* the broadcast.
Reading a channel relay as two events on two sockets is the thing this view
exists for.

## Levels

`FT_IRC_LOG` takes a name or a digit. Each level includes everything above it.

| Level | | Shows |
| --- | --- | --- |
| `quiet` | 0 | nothing at all |
| `error` | 1 | startup banner, fatal conditions |
| `warn` | 2 | + recoverable problems |
| `info` | 3 | + connection lifecycle — **the default** |
| `debug` | 4 | + per-session detail and traffic counters |
| `trace` | 5 | + every protocol line, both directions |

An unrecognised value keeps the default rather than guessing — a typo in
`FT_IRC_LOG` should not silently turn the console off.

It is read from the **environment**, not the config file, because the config
file is a full-tier feature and the trace has to work on the `make mandatory`
binary — the one that gets defended.

## The columns

```
18:40:17   fd   6   <<   bob        JOIN #room       [JOIN]
└ time     └ fd     └ dir └ peer    └ the RFC line   └ annotation
```

* **fd** — the socket. The only identity a connection has before it
  registers, which is exactly when things go wrong.
* **direction** — `<<` from a client, `>>` towards one. Chosen so
  `grep '>>'` isolates one side without matching protocol text.
* **peer** — the nickname, or `*` when it has none yet — the same placeholder
  the protocol itself uses in a pre-registration numeric.
* **line** — verbatim RFC 2812, credentials redacted. Still a valid IRC line,
  so the log stays re-readable as protocol.
* **annotation** — the command, and for a numeric its RFC name. This is what
  turns `:ft_irc 462 bob :You may not reregister` from a string into a
  diagnosis. The name table is built from the `Replies.hpp` macros themselves,
  so it cannot drift from what the server actually sends.

At `debug` and above, each session also reports its own totals when it closes,
and the process prints a grand total at shutdown:

```
18:40:18  fd   5  --  alice left (Quit) — 8 in / 14 out, 146 B / 738 B
 ℹ  traffic: 2 session(s), 16 lines in / 31 out, 292 B in / 1476 B out
```

## Credentials are redacted

A console log gets pasted into bug reports and shown during a defense. Printing
the server password there would be a real leak, and a trace nobody can show
anyone is worth nothing. Redacted in **both** directions:

| Line | Logged as |
| --- | --- |
| `PASS hunter2` | `PASS ***` |
| `JOIN #demo secretkey` | `JOIN #demo ***` |
| `MODE #demo +k topsecret` | `MODE #demo +k ***` |
| `:ft_irc 324 bob #k +kl topsecret 5` | `:ft_irc 324 bob #k +kl *** 5` |

Three details that took a bug each to get right:

* **The channel is not the key.** An early version starred out `#demo` in the
  `JOIN` echo, because the redactor counted the outbound line's `:prefix` as
  the command and shifted every parameter by one. It redacted the wrong token
  *and still printed the secret*.
* **`+k` is not the key either.** The same off-by-one starred `+k` in a `MODE`
  broadcast and left `topsecret` in plain sight. The key's parameter index
  depends on where `k` sits among the parameter-taking letters, so the
  redactor walks the mode string the way `handleChannelMode` does.
* **`324 RPL_CHANNELMODEIS` carries the key.** It is the one *reply* holding a
  credential — the reason the MODE query path requires membership at all —
  and it leaked until it was redacted like the commands.

Nothing else is filtered. Message text is shown in full: it is what the users
are actually saying, and hiding it defeats the purpose.

## Where it hooks in

Two functions, because the server has only two places a line can cross the
socket boundary:

| Direction | Hook | Why it cannot be bypassed |
| --- | --- | --- |
| in | `Server::handleMessage()` | every extracted line lands here, before parsing and before the registration gate — so malformed lines that never become a `Message` are traced too, and those are exactly the ones worth seeing |
| out | `Client::queueMessage()` | the single choke point every reply, relay and broadcast funnels through (the same reason the 512-byte cap lives there) |

The outbound hook fires **after** truncation, so the log shows what actually
reaches the peer rather than what the caller passed in.

## Cost

Every entry point is behind `Log::enabled()`, so below `trace` the tracer does
no formatting and allocates nothing — it takes a counter increment and
returns. The counters run at all levels, which is why the shutdown total is
correct even at `quiet`.

## Implementation

| File | Role |
| --- | --- |
| `include/Log.hpp`, `src/Log.cpp` | levels, sink interface, plain renderer |
| `include/IrcTrace.hpp`, `src/IrcTrace.cpp` | redaction, RFC annotation, counters |
| `src/extras/FancyLogSink.cpp` | coloured renderer (full tier) |

`IrcTrace` is **kernel** code: it links into all three tiers and depends only
on libcpp's `str` module, which every tier already compiles. The mandatory
binary gets the same trace and the same redaction, without colour.

The full tier's `FancyLogSink` colours the direction, peer, command and
trailing separately using libcpp's `Srgb`, and paints a line carrying a 4xx/
5xx/6xx numeric red — so a failing session is visible by scrolling rather than
by reading. It deliberately does **not** route the trace through `TermWriter`:
callouts are boxed, and one box per protocol line is unreadable at line rate.

> `FancyLogSink::write()` previously had no `case` for the debug and trace
> kinds, so every one of them was silently swallowed on the full tier while
> the counters kept incrementing. If the trace ever goes quiet on `make` but
> works on `make mandatory`, that switch is the first place to look.

## See also

* [scenarios/](scenarios/README.md) — the same protocol, as walkthroughs
* [RFC-CONFORMANCE.md](RFC-CONFORMANCE.md) — what the server guarantees
* `scripts/simulation.sh` — a populated server to point the trace at
