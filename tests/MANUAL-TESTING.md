# ft_irc — Manual Testing Playbook

Every test in this document was actually run. Each one states what it checks, the exact
command, and what a correct server does.

You do not need to have followed the earlier sessions to use this. Run the setup, then pick
any section — they are independent unless stated otherwise.

**Two audiences:**
- *Teammates*: use this to re-verify behaviour after changing code.
- *Evaluators*: sections 1–7 are a complete walkthrough of the mandatory requirements.

---

## Setup

### Build and run

```bash
make fclean && make mandatory
./ircserv 6667 test123
```

The mandatory binary is the one that matters. `bonus` and `full` add a bot, file transfer
and config-gated extensions that can interfere with parsing tests.

### Reference client: HexChat

The subject requires that using HexChat with this server feels *"similar to using it with
any official IRC server"*, so HexChat is the primary client.

1. **Network List** (`Ctrl+S`) → **Add** → name it e.g. `ft_irc local`.
2. **Edit** → server `127.0.0.1/6667`, **uncheck** "Use SSL".
3. Put `test123` in the **Password** field.
4. Set **Nick name** to something **9 characters or shorter** (e.g. `bob`). See test 2.1 for
   why this matters.

### Reading raw protocol traffic

**This is the single most important habit in this document.** The chat window is a
rendering, not the truth. It merges, splits and reformats lines.

**Window → Raw Log.** Leave it open for the whole session.

- `<<` = sent by HexChat
- `>>` = received from the server

The Raw Log only captures from the moment you open it. To capture the connection burst:
connect, open the Raw Log, then type `/reconnect`.

To send a raw command without HexChat reinterpreting it: `/quote <command>`

### Second client: netcat

Many tests need two clients. `nc` is used as the scriptable one.

```bash
nc -C 127.0.0.1 6667      # -C sends CRLF line endings
```

For repeatable tests, drive it with `printf` and `sleep` so you control the exact timing of
each write:

```bash
{ printf 'PASS test123\r\nNICK alice\r\nUSER alice 0 * :A\r\n'; sleep 5; } | nc 127.0.0.1 6667
```

**Two traps worth knowing before you start:**

1. **`nc` only exits when its stdin is at EOF *and* the socket is closed.** This is the
   single most misleading thing about using `nc` as a test client. If a `sleep` holds stdin
   open, the server can close the connection and `nc` will keep running, waiting for input —
   so the moment you observe the process end is the end of your `sleep`, not the server's
   decision. This produced a false bug report that survived several rounds of review.

   When the *timing* of a disconnect matters, do not use `nc`. Use a client that detects
   `recv() == 0` and timestamps it (see 7.3). When timing does not matter, stamp each line
   anyway:
   ```bash
   ... | nc 127.0.0.1 6667 | while IFS= read -r l; do echo "$(date +%T) $l"; done
   ```
2. Never press `Ctrl+D` on an empty line in interactive `nc` — that closes your own socket.
   If you then see the server report a disconnect, you caused it.

---

## 1. Registration and authentication

### 1.1 Normal registration

*Checks the `PASS` → `NICK` → `USER` sequence and the welcome burst.*

Connect with HexChat using the correct password. In the Raw Log you should see `001`, `002`,
`003`, `004`, `005`, then `422` (no MOTD file) or `375`/`372`/`376` (MOTD present).

### 1.2 Wrong password

*Checks that the client is told why it was rejected, rather than silently dropped.*

Set the **Password** field to something wrong, keep the server on `test123`, connect.

```
>> :ft_irc 464 bob :Password incorrect
```

HexChat should display "Password incorrect". A silent close would leave an evaluator with no
idea why they cannot connect.

Scriptable version, three timings — all three should produce the `464`:

```bash
# everything in one write (the tightest case)
{ printf 'PASS wrong\r\nNICK n1\r\nUSER n1 0 * :N\r\n'; sleep 3; } | nc 127.0.0.1 6667 | xxd

# with pauses between lines
{ printf 'PASS wrong\r\n'; sleep 1; printf 'NICK n2\r\n'; sleep 1; \
  printf 'USER n2 0 * :N\r\n'; sleep 3; } | nc 127.0.0.1 6667 | xxd

# PASS arriving after NICK+USER already completed the pair
{ printf 'NICK n3\r\nUSER n3 0 * :N\r\n'; sleep 1; \
  printf 'PASS wrong\r\n'; sleep 3; } | nc 127.0.0.1 6667 | xxd
```

`xxd` is deliberate: if the server closes without sending anything, the output is *empty*,
which is an observable result rather than an ambiguous absence.

### 1.3 Commands before registration

*Checks that only `CAP`, `PASS`, `NICK`, `USER`, `QUIT` and `PONG` are accepted early.*

```bash
{ printf 'PASS test123\r\nNICK n4\r\n'; sleep 1; \
  printf 'JOIN #early\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `451 :You have not registered` (or equivalent) rather than a successful join.

---

## 2. Nickname handling

### 2.1 Long nickname

*Checks that a nickname longer than `NICKLEN` is truncated rather than rejected.*

```bash
{ printf 'PASS test123\r\nNICK abcdefghijklmno\r\nUSER x 0 * :X\r\n'; sleep 3; } | nc 127.0.0.1 6667
```

Expect a full `001`-`005` burst under the truncated nickname (`abcdefghi`), **not** a `432`.

This matters more than it looks. HexChat's fallback appends suffixes (`_a`, `_1`) which make
the nickname *longer*, so if the server rejects long nicknames instead of truncating them,
every retry fails for the same reason and the connection dies — an evaluator whose default
nickname is 10+ characters cannot connect at all. This was a real bug; it is now fixed, and
this test is the regression check.

### 2.1b Collision after truncation

*The risk introduced by truncation: two different long nicknames can truncate to the same
short one.*

With the client from 2.1 still connected as `abcdefghi`:

```bash
{ printf 'PASS test123\r\nNICK abcdefghiZZZZZ\r\nUSER y 0 * :Y\r\n'; sleep 3; } | nc 127.0.0.1 6667
```

Expect `433 * abcdefghi :Nickname is already in use`, and registration must **not** complete.
Two clients sharing a nickname would be worse than the original bug.

Note the `433` names the *truncated* nickname, so the client can see what it actually
collided with.

### 2.1c Truncation after registration

*The same rule must apply to nickname changes, not just registration.*

```bash
{ printf 'PASS test123\r\nNICK short\r\nUSER z 0 * :Z\r\n'; sleep 1; \
  printf 'NICK qwertyuiopasd\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `:short!z@127.0.0.1 NICK :qwertyuio`. If the target truncates onto a nickname already
in use, expect `433` and the client keeps its current nickname.

### 2.2 Underscore in nickname

*Confirms the length truncation above is about length, not character set.*

```bash
{ printf 'PASS test123\r\nNICK bo_b\r\nUSER x 0 * :X\r\n'; sleep 3; } | nc 127.0.0.1 6667
```

Should be accepted — RFC 2812 explicitly allows `_`.

### 2.3 Duplicate nickname

*Checks collision handling in three different code paths.*

With `bob` already connected in HexChat:

```bash
# a) before registration, same case
{ printf 'PASS test123\r\nNICK bob\r\nUSER e1 0 * :E\r\n'; sleep 3; } | nc 127.0.0.1 6667

# b) before registration, different case (tests ASCII casemapping)
{ printf 'PASS test123\r\nNICK BOB\r\nUSER e2 0 * :E\r\n'; sleep 3; } | nc 127.0.0.1 6667

# c) after registration
{ printf 'PASS test123\r\nNICK e3\r\nUSER e3 0 * :E\r\n'; sleep 1; \
  printf 'NICK bob\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `433 :Nickname is already in use` in all three. In (a) and (b) registration must not
complete — no `001`. In (c) the client keeps its existing nickname.

### 2.4 Changing the case of your own nickname

*Casemapping must not prevent you from re-capitalising your own nick.*

In HexChat, as `bob`: `/quote NICK BOB`

```
>> :bob!ftirc@127.0.0.1 NICK :BOB
```

Should be accepted. A `433` here would be wrong — you are not colliding with anyone but
yourself.

---

## 3. Channels

### 3.1 Joining a channel

*Checks the full join burst and what HexChat does with it.*

In HexChat: `/join #test`

Watch the Raw Log. HexChat sends `MODE #test` and `WHO #test` on its own immediately after
the `JOIN` — the server must handle both. Expect `353` (names), `366` (end of names),
`324` (modes), `329` (creation time), then `352`/`315` for the WHO.

### 3.2 Channel name casemapping

*Checks that `#TEST` and `#test` are the same channel.*

With HexChat in `#TEST`:

```bash
{ printf 'PASS test123\r\nNICK e6\r\nUSER e6 0 * :E\r\n'; sleep 1; \
  printf 'JOIN #test\r\n'; sleep 3; } | nc 127.0.0.1 6667
```

The `353` reply should list the HexChat user, proving it is one channel and not two. The
`329` creation timestamps should match on both clients.

### 3.3 Key-protected channel

*Checks `+k` enforcement.*

In HexChat: `/join #lock` then `/mode #lock +k secreto`

```bash
{ printf 'PASS test123\r\nNICK e5\r\nUSER e5 0 * :E\r\n'; sleep 1; \
  printf 'JOIN #lock\r\n'; sleep 1; \
  printf 'JOIN #lock wrongkey\r\n'; sleep 1; \
  printf 'JOIN #lock secreto\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `475` for the first two, a successful join for the third. Also check the HexChat Raw
Log: the failed attempts must **not** produce any `JOIN` line for other members.

### 3.4 Invalid channel limit

*Checks that a malformed `+l` argument is refused and never applied.*

```bash
{ printf 'PASS test123\r\nNICK m1\r\nUSER m1 0 * :M\r\n'; sleep 1; \
  printf 'JOIN #lim\r\n'; sleep 1; \
  printf 'MODE #lim +l abc\r\n'; sleep 1; \
  printf 'MODE #lim +l -5\r\n'; sleep 1; \
  printf 'MODE #lim +l\r\n'; sleep 1; \
  printf 'MODE #lim\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `696 :Invalid channel limit` for the bad values, `461 :Not enough parameters` when the
argument is missing, and — most importantly — the final `MODE #lim` must show that **nothing
was applied**.

Note: a real server (Solanum) stays silent instead of sending `696`. Explaining the rejection
is a deliberate choice here, not a defect.

### 3.5 Kicking a user

*Checks `KICK` with and without an explicit reason.*

With HexChat as channel operator and a second client `k1` in `#kick`:

```
/kick k1
/kick k1 because reasons
```

Both should produce a `KICK` line visible to every member, and the target must disappear from
the user list. Minor known difference: the default reason here is the *kicker's* nickname,
whereas a real server uses the *kicked* user's nickname.

---

## 4. Message routing

### 4.1 Message to a channel you are not in

*Checks that outsiders cannot write into a channel.*

With HexChat in `#a`, and the test client **not** in it:

```bash
{ printf 'PASS test123\r\nNICK e4\r\nUSER e4 0 * :E\r\n'; sleep 1; \
  printf 'PRIVMSG #a :I am not here\r\n'; sleep 1; \
  printf 'PRIVMSG #nada :no such channel\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `404 :Cannot send to channel` and `403 :No such channel`.

**Then check the HexChat Raw Log.** If the message arrived anyway, that is a serious leak —
any client could write into any channel.

### 4.2 Normal channel message

*Baseline check that messages reach other members and not the sender.*

Two clients in the same channel; send a `PRIVMSG` from one. The other must receive
`:sender!user@host PRIVMSG #chan :text`. The sender must **not** receive an echo of their own
message.

---

## 5. Partial commands and line parsing

The subject explicitly requires the server to reassemble commands split across multiple
packets. Section 5.1 is the literal test; the rest cover the edges around it.

### 5.1 The subject's test, by hand

```bash
nc -C 127.0.0.1 6667
```

Register first (`PASS test123`, `NICK d1`, `USER d1 0 * :D`) so the split command produces a
`421` that echoes the reassembled word back at you.

Then type `com` + `Ctrl+D`, `man` + `Ctrl+D`, `d` + `Enter`.

```
>> :ft_irc 421 d1 COMMAND :Unknown command
```

The word `command`, reassembled from three separate writes, is the proof.

**To make this verifiable rather than merely plausible**, run it under `strace` — the terminal
looks identical whether or not `Ctrl+D` actually flushed:

```bash
strace -f -e trace=write nc -C 127.0.0.1 6667
```

You should see one `write()` per `Ctrl+D`.

### 5.2 Command name split across writes

```bash
{ printf 'PASS test123\r\nNICK d2\r\nUSER d2 0 * :D\r\n'; sleep 1; \
  printf 'PI'; sleep 1; printf 'NG :tok'; sleep 1; printf '\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `PONG ... :tok`.

### 5.3 Split between CR and LF

*The nastiest edge: a parser that scans for `\r\n` may drop a lone trailing `\r`.*

```bash
{ printf 'PASS test123\r\nNICK d3\r\nUSER d3 0 * :D\r\n'; sleep 1; \
  printf 'PING :crlf\r'; sleep 1; printf '\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect `PONG ... :crlf`.

### 5.4 Two commands in one write

*The inverse case: the buffer must not stop after the first line.*

```bash
{ printf 'PASS test123\r\nNICK d4\r\nUSER d4 0 * :D\r\n'; sleep 1; \
  printf 'PING :one\r\nPING :two\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect **two** `PONG` replies.

### 5.5 Registration itself, fragmented

*The highest-risk variant: if the password is compared against a truncated string, the
connection is refused.*

```bash
{ printf 'PASS test'; sleep 1; printf '123\r\nNI'; sleep 1; \
  printf 'CK d5\r\nUSER d5 0 '; sleep 1; printf '* :D\r\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect a complete `001`–`005` burst, not a `464`.

### 5.6 Bare LF line ending

```bash
{ printf 'PASS test123\nNICK d6\nUSER d6 0 * :D\n'; sleep 1; \
  printf 'PING :bare\n'; sleep 2; } | nc 127.0.0.1 6667
```

Expect normal registration and a `PONG`. Real servers tolerate `\n` without `\r`.

---

## 6. Disconnection

### 6.1 QUIT with shared channels

*Checks that a departing user generates exactly one `QUIT` per observer, regardless of how
many channels they share.*

HexChat joins `#a`, `#b` and `#c`. Then:

```bash
{ printf 'PASS test123\r\nNICK carol\r\nUSER carol 0 * :C\r\n'; sleep 1; \
  printf 'JOIN #a\r\n'; sleep 1; \
  printf 'JOIN #b\r\n'; sleep 1; \
  printf 'JOIN #c\r\n'; sleep 2; \
  printf 'QUIT :bye\r\n'; sleep 1; } | nc 127.0.0.1 6667
```

Expect **one** `QUIT` line in the HexChat Raw Log, not three.

Then confirm there is no ghost left behind — better evidence than eyeballing the user list:

```
/quote WHO #a
/quote WHO #b
/quote WHO #c
```

The departed user must appear in none of them.

### 6.2 No leak to unrelated clients

*A `QUIT` must only reach clients who share a channel.*

HexChat in `#a`; test client joins `#z` only and quits. HexChat must receive nothing.

### 6.3 Abrupt disconnect

*Different code path from the `QUIT` command — and the one an evaluator triggers by closing
a window.*

Run `nc -C 127.0.0.1 6667` interactively, register, join a channel, then kill it with
`Ctrl+C`.

Expect a single `QUIT :Connection closed` in the observer's Raw Log, and clean removal from
the channel.

> Note: `Connection closed` is the reason the server emits when **the client** closes its own
> socket. Do not read it as the server ejecting anyone.

---

## 7. Keepalive and timeouts

Note that HexChat sends its own `PING` every 30 seconds, so it never reaches an idle timeout.
These tests need `nc`.

### 7.1 Server PING after inactivity

```bash
{ printf 'PASS test123\r\nNICK t1\r\nUSER t1 0 * :T\r\n'; sleep 400; } \
  | nc 127.0.0.1 6667 \
  | { while IFS= read -r l; do echo "$(date +%T) $l"; done; \
      echo "$(date +%T) --- socket closed ---"; }
```

Expect `:ft_irc PING :ft_irc` roughly 120 seconds after registration (the idle sweep runs
every 30 seconds, so anything in the 120–150 s range is correct).

### 7.2 Activity resets the idle timer

```bash
{ printf 'PASS test123\r\nNICK t3\r\nUSER t3 0 * :T\r\n'; sleep 90; \
  printf 'PING :keep\r\n'; sleep 250; } | nc 127.0.0.1 6667 \
  | { while IFS= read -r l; do echo "$(date +%T) $l"; done; \
      echo "$(date +%T) --- socket closed ---"; }
```

The server's `PING` should arrive ~120 s after the **client's last line**, not 120 s after
registration. The server must also answer the client's `PING` with `PONG ... :keep`.

### 7.3 Timeout disconnect

*Checks that a registered client which stops responding is eventually dropped.*

**Do not test this with `nc`** — see the warning in the setup section. `nc` will not exit
when the server closes the connection if its stdin is still open, so you will measure your
own `sleep` instead of the server. Use a client that reports `recv() == 0` with a timestamp:

```bash
./tools/mute_client.sh --silent 127.0.0.1 6667 test123 mute1 400
```

`mute_client.sh --silent` performs exactly one burst of writes (the registration) and never
writes again, while continuing to read and timestamp everything that arrives.
The socket stays fully open — no `shutdown`, no `close` — so this is a genuinely silent
client rather than one that has signalled it is going away.

Expected output:

```
t+  0.0s  registration sent
t+144.1s  :ft_irc PING :ft_irc
t+264.3s  *** SERVER CLOSED THE CONNECTION ***
```

Correct behaviour: a `PING` around 120 s after the last activity (130-150 s is fine, the
idle sweep runs every 30 s), then a close 120 s after that `PING`.

If you want to confirm the client really is silent, run it under `strace`:

```bash
strace -f -e trace=sendto,write,sendmsg,shutdown -tt -o /tmp/mute.trace \
  ./tools/mute_client.sh --silent
grep -c 'sendto\|sendmsg' /tmp/mute.trace
```

## 8. Limits

### 8.1 Send queue overflow (64 KiB)

*Checks that a client which stops reading is eventually dropped instead of buffering forever.*

Three terminals, started in this order.

**1. Victim — never drains its socket:**
```bash
{ printf 'PASS test123\r\nNICK v1\r\nUSER v1 0 * :V\r\n'; sleep 2; \
  printf 'JOIN #flood\r\n'; sleep 900; } | nc 127.0.0.1 6667 | { sleep 900; }
```

**2. Observer — reads normally, filters the noise:**
```bash
{ printf 'PASS test123\r\nNICK o1\r\nUSER o1 0 * :O\r\n'; sleep 2; \
  printf 'JOIN #flood\r\n'; sleep 900; } | nc 127.0.0.1 6667 \
  | grep --line-buffered -a 'QUIT\|ERROR\|KILL'
```

**3. Flooder — start once the other two are in the channel:**
```bash
PAY=$(printf 'A%.0s' $(seq 1 400))
{ printf 'PASS test123\r\nNICK f1\r\nUSER f1 0 * :F\r\n'; sleep 2; \
  printf 'JOIN #flood\r\n'; sleep 2; \
  for i in $(seq 1 5000); do printf 'PRIVMSG #flood :%s\r\n' "$PAY"; done; \
  sleep 30; } | nc 127.0.0.1 6667
```

Expect the observer to report `:v1!v1@127.0.0.1 QUIT :SendQ exceeded`, and the server log to
show `client disconnected: v1 (SendQ exceeded)`.

~2 MB is deliberately generous: kernel send/receive buffers and the pipe absorb several
hundred KB before the server's own queue starts growing. If nothing happens, raise the loop
count before drawing any conclusion.

### 8.2 Long line (512-byte limit)

*Checks truncation at the protocol line limit.*

```bash
PAY=$(printf 'A%.0s' $(seq 1 600))
{ printf 'PASS test123\r\nNICK g1\r\nUSER g1 0 * :G\r\n'; sleep 1; \
  printf 'JOIN #long\r\n'; sleep 1; \
  printf 'PRIVMSG #long :%s\r\n' "$PAY"; sleep 3; } | nc 127.0.0.1 6667
```

Observe from a **second** client in `#long`. The relayed message should be truncated so that
the whole line — `:nick!user@host PRIVMSG #long :` + payload + CRLF — fits in 512 bytes. With
short nicknames that lands around 470–480 characters of payload.

Keep the sending client alive (the `sleep`) so that any disconnect you observe is the
server's decision and not your own EOF.

---

## 9. Comparing against a real server

When a result looks wrong, the question is not "what does the RFC say" but "what does a real
server do". The reference used here is Libera Chat, which runs Solanum — the same family of
server HexChat is normally used with.

### 9.1 Connecting

1. **Network List** → select **Libera.Chat** → **Connect**.
2. Leave the **Password** field empty. On Libera that field is for NickServ/SASL, not a
   server password.
3. Open **Window → Raw Log** the same way as before.

Be considerate: it is production infrastructure with flood protection. Run one test at a
time and use `##ftirc-lab-<something>` for scratch channels.

### 9.2 Useful comparisons

| What to compare | How |
|---|---|
| Registration burst, `004` and `005` | Connect and read the Raw Log |
| Behaviour on an over-long nickname | `/quote NICK abcdefghijklmnopqrstuvwxyz` (Libera's `NICKLEN` is 16) |
| Re-capitalising your own nickname | `/quote NICK <YOURNICK-IN-CAPS>` |
| Join burst and whether `324`/`329` are automatic | `/join ##ftirc-lab-test` |
| Default `KICK` reason | Two clients in a scratch channel, then `/kick <nick>` |
| Invalid `+l` argument | `/quote MODE ##ftirc-lab-test +l abc` |

Two caveats when comparing:

- Libera uses `CASEMAPPING=rfc1459`; this server uses `ascii`. Both are valid and announced.
  They agree on A–Z but differ on `[ ] \ ~`, so Libera cannot be used as a reference for
  those characters.
- Libera negotiates capabilities such as `server-time` and `extended-join`, which add `@time=`
  tags and extra `JOIN` parameters. This server does not announce them, so HexChat will not
  request them. That difference is expected, not a defect.

### 9.3 Verify the command actually went out

Before reading any reply, confirm the `<<` line appears in the Raw Log. HexChat sometimes
swallows or rewrites input, and a reply to a command you did not send proves nothing.

---

## Known issues at time of writing

| # | Issue | Severity | Test |
|---|---|---|---|
| 1 | ~~Nicknames over 9 characters are rejected instead of truncated; HexChat cannot connect~~ — fixed: now truncated to NICKLEN (T1) | Fixed | 2.1 |
| 2 | `324`/`329` sent twice on join (HexChat de-duplicates them on screen) | Low | 3.1 |
| 3 | `CHANMODES` puts `l` in the wrong group; should be `,k,l,it` | Cosmetic | 1.1 |
| 4 | Channel name capitalisation differs between the `JOIN` echo and the numerics | Cosmetic | 3.2 |
| 5 | Default `KICK` reason uses the kicker's nick; real servers use the kicked user's | Cosmetic | 3.5 |
| 6 | No `~` prefix on the username when there is no ident response | Cosmetic | 1.1 |

Everything else in this document has been verified as behaving correctly — including the
idle timeout, which was reported as broken for a while and turned out to be a measurement
artifact of using `nc` as the test client.

---

## 10. Bonus features

These require the bonus build. Everything below was verified against it.

```bash
make fclean && make bonus
./ircserv 6667 test123
```

### A note on syntax when using `nc`

HexChat builds protocol lines for you: `/msg ircbot !help` becomes
`PRIVMSG ircbot :!help`. From `nc` you type the whole line yourself, and **the space before
the colon is mandatory** — it separates the target from the message text.

Two ways to get it wrong, both of which the server rejects correctly:

```
PRIVMSG #ircbot :!help    -> 403 :No such channel      (the # makes it a channel name)
PRIVMSG ircbot:!help      -> 412 :No text to send      (no space, so no text parameter)
```

### 10.1 Is the bonus build actually running?

*Do this first — everything else in this section depends on it.*

```
/msg ircbot !help
```

A reply means the bonus tier is linked. `401 ircbot :No such nick/channel` means you are
running the mandatory binary.

`ircbot` is **not** a separate process and **not** a connected client. It is a virtual user
inside the server that only claims private messages addressed to its nickname. Typing
`!help` into a channel does nothing — it is just channel text.

### 10.2 Bot commands

```
/msg ircbot !time
/msg ircbot !info
/msg ircbot !info #test
/msg ircbot !joke
```

`!info #test` should report the member count and modes of that channel. Run `!joke` several
times to confirm it rotates rather than repeating one hardcoded string.

### 10.3 The bot's nickname is reserved

```
/quote NICK ircbot
```

Expect `433 :Nickname is already in use`. If a client can take this nickname, it can
impersonate the bot.

### 10.4 Unknown bot command

```
/msg ircbot !nonsense
```

Expect a reply, not silence:
`:ircbot PRIVMSG you :Unknown command. Type !help for available commands.`

### 10.5 FILE transfer — happy path

This is the server-mediated `FILE` protocol (base64 relay). The server never decodes the
payload and never touches disk.

Two terminals running `nc -C 127.0.0.1 6667`. Use a tiny file so it fits in one chunk —
`hello world!` is 12 bytes and its base64 is `aGVsbG8gd29ybGQh`.

**Receiver (bob)** — register first:
```
PASS test123
NICK bob
USER bob 0 * :Bob
```

**Sender (alice)**:
```
PASS test123
NICK alice
USER alice 0 * :Alice
FILE SEND bob hello.txt 12
```

Expected exchange:
```
alice -> FILE SEND bob hello.txt 12
alice <- :ft_irc NOTICE alice :FILE 1 offered to bob
  bob <- :alice!alice@127.0.0.1 FILE OFFER 1 hello.txt 12
  bob -> FILE ACCEPT 1
alice <- :bob!bob@127.0.0.1 FILE OK 1
alice -> FILE DATA 1 aGVsbG8gd29ybGQh
  bob <- :alice!alice@127.0.0.1 FILE DATA 1 aGVsbG8gd29ybGQh
alice -> FILE END 1
  bob <- :alice!alice@127.0.0.1 FILE END 1 12
```

**The proof is that the payload bob receives is character-for-character identical to what
alice sent.** To see it in plain text, decode it — but in a *separate shell*, not inside
`nc`. Anything typed into `nc` is sent to the server as an IRC line, which is why shell
commands come back as `421 ECHO :Unknown command`.

```bash
echo 'aGVsbG8gd29ybGQh' | base64 -d      # -> hello world!
```

Note the transfer id (`1` here) increments per offer; use the id the server actually gave
you.

### 10.6 FILE transfer — rejection

```
alice -> FILE SEND bob otro.txt 12
  bob <- :alice!alice@127.0.0.1 FILE OFFER 2 otro.txt 12
  bob -> FILE REJECT 2
alice <- :bob!bob@127.0.0.1 FILE NO 2
```

The sender must be told, not left waiting.

### 10.7 Sending data after a rejection

*The one case with real potential to bite: if data still flows after a refusal, `REJECT`
means nothing.*

From alice, after the rejection above:
```
FILE DATA 2 aGVsbG8gd29ybGQh
```

Expect `:ft_irc NOTICE alice :FILE: no transfer with id 2`, and **check the receiver's
terminal**: bob must receive nothing at all.

### 10.8 FILE transfer to a non-existent user

```
FILE SEND nadie x.txt 12
```

Expect `:ft_irc NOTICE you :FILE: no such nick nadie` — an explicit error, not silence.

`FILE` errors are reported as server `NOTICE`s rather than numerics. That is deliberate:
`FILE` is not an RFC command, so no standard numeric applies to it.

### 10.9 DCC relay (CTCP passthrough)

Separate from 10.5–10.8. The server implements no DCC logic at all; it just relays the
`\x01`-wrapped CTCP payload untouched, which is all HexChat needs to show its file transfer
dialog.

This one cannot be typed by hand — you need the literal `\x01` bytes. Two scripted
terminals.

**Receiver** (`cat -v` makes the control bytes visible):
```bash
{ printf 'PASS test123\r\nNICK dan\r\nUSER dan 0 * :D\r\n'; sleep 20; } \
  | nc 127.0.0.1 6667 | cat -v
```

**Sender**, once `dan` is registered:
```bash
{ printf 'PASS test123\r\nNICK eve\r\nUSER eve 0 * :E\r\n'; sleep 2; \
  printf 'PRIVMSG dan :\001DCC SEND test.txt 2130706433 12345 100\001\r\n'; sleep 3; } \
  | nc 127.0.0.1 6667
```

Expected on the receiver:
```
:eve!eve@127.0.0.1 PRIVMSG dan :^ADCC SEND test.txt 2130706433 12345 100^A^M
```

The `^A` markers are the `\x01` bytes arriving intact. The trailing `^M` is the CR of the
line ending, which is normal. If the server stripped the `\x01` bytes, HexChat would never
show a transfer dialog and DCC would be dead.

---

## Known issues at time of writing

| # | Issue | Severity | Test |
|---|---|---|---|
| 1 | `324`/`329` sent twice on join (HexChat de-duplicates them on screen) | Low | 3.1 |
| 2 | `CHANMODES` puts `l` in the wrong group; should be `,k,l,it` | Cosmetic | 1.1 |
| 3 | Channel name capitalisation differs between the `JOIN` echo and the numerics | Cosmetic | 3.2 |
| 4 | Default `KICK` reason uses the kicker's nick; real servers use the kicked user's | Cosmetic | 3.5 |
| 5 | No `~` prefix on the username when there is no ident response | Cosmetic | 1.1 |
| 6 | The bot uses a bare-nick prefix (`:ircbot PRIVMSG …`) where real services use `nick!user@host` | Cosmetic | 10.2 |

Everything else in this document has been verified as behaving correctly — including the
idle timeout, which was reported as broken for a while and turned out to be a measurement
artifact of using `nc` as the test client.