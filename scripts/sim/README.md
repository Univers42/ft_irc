# Simulation harness

Bring up a whole populated ft_irc environment — server, ten users, several
channels, operators, an ongoing conversation — with one command, and free all
of it with another.

```bash
scripts/simulation.sh                 # 10 users on netcat, scripted conversation
scripts/simulation.sh --hexchat 2     # first two as real HexChat windows
scripts/shutdown_simulation.sh        # free everything
```

Everything runs **in the background**; the shell comes straight back to you.
State and logs live in `.sim/` at the repo root (gitignored).

---

## What comes up

The cast is `personas.conf` — edit that one file and the whole environment
changes:

```
alice    | auto    | #general,#dev,#ops   | op   | Alice Liddell
bob      | auto    | #general,#dev        | op   | Bob Martin
carol    | auto    | #general,#random     | user | Carol Danvers
...
judy     | auto    | -                    | user | Judy Hopps
```

The default cast is built to exercise the overlaps that matter: users in
several channels at once, a channel with one member, a lurker in none, ops who
are ordinary members elsewhere, and two users who share every channel.

Ops connect first, so the first op listing a channel creates it and owns it;
every other op on that channel is then granted `+o` by the owner. That
ordering is what makes `MODE`, `KICK` and `INVITE` in a scenario file
predictable.

**Nicks are validated before anything connects.** A roster entry longer than 9
characters is rejected rather than silently truncated by the server — that
truncation (`probeclient` → `probeclie`) is the single most common way to lose
an afternoon here.

---

## Driving it

One interface for both client kinds. A HexChat client receives raw IRC through
`/quote`, so the same command works whether the target is a socket or a GUI:

```bash
scripts/simulation.sh --send judy 'JOIN #general'
scripts/simulation.sh --say  judy '#general' 'hello everyone'
scripts/simulation.sh --cmd  alice 'say typed into the GUI'   # HexChat command
```

Two background drivers:

* `--scenario FILE` (default `scenario_default.conf`) replays a timed
  conversation — `<delay> <nick> <raw IRC line>`, delays relative to the
  previous line, so the file reads as a timeline.
* `--chatter` keeps generating small talk in whatever channels people are
  actually in, until shutdown.

---

## Reading the logs

```bash
scripts/simulation.sh --status            # who is up, and where
scripts/simulation.sh --logs alice        # one client's session
scripts/simulation.sh --tail alice        # follow it live
scripts/simulation.sh --grep 'PRIVMSG #dev'   # across every client at once
scripts/simulation.sh --server-log        # the server console
```

Every netcat client keeps two logs, because they answer different questions:

| File | Contents |
| --- | --- |
| `.sim/clients/<nick>/raw.log` | exactly what the server sent, CR included — **grep this** for protocol work |
| `.sim/clients/<nick>/rx.log` | the same lines, timestamped, CR stripped — **read this** |
| `.sim/clients/<nick>/tx.log` | what the simulation sent as that user |

HexChat clients log per tab under `.sim/hexchat/<nick>/logs/ftircsim/`.

---

## Checking the naming rules

```bash
scripts/simulation.sh --verify-names
```

46 probes against RFC 2812 §2.3.1 and the server's own `005` tokens: the legal
nickname grammar (letters, digits, `[ ] \ ^ _ { | }`, `-`), the illegal
characters, `NICKLEN=9` truncation, truncation-induced collisions,
`CASEMAPPING=ascii`, `CHANTYPES=#`, `CHANNELLEN=50`, and that a channel is
echoed back in its stored spelling rather than the caller's.

The report separates three outcomes deliberately:

* **PASS** — behaves as RFC 2812 and its own 005 tokens say it should.
* **FAIL** — the server contradicts what it advertises. A bug.
* **DIVERGE** — deliberately stricter or looser than RFC 2812. Not a bug, but
  worth knowing before an evaluator finds it.

Current result: **45 pass, 1 diverge, 0 fail.** The divergence is real — RFC
2812 puts `` ` `` (%x60) inside its `special` range, so `` z`tick `` is a legal
nickname; `Server::isValidNickname` enumerates the specials by hand and leaves
it out, answering `432`.

---

## Fuzzing MODE

```bash
scripts/simulation.sh --fuzz-mode [N]     # N random cases, default 150
```

MODE is the widest parser surface in the server: a free-form sign/letter string
plus a positional parameter list, where each letter decides for itself whether
it consumes one. The fuzzer drives ~80 hand-picked edge cases plus N random
ones and checks nine invariants on every reply — liveness, well-formed lines,
no silent no-ops, sign sanity, parameter accounting, round-trip acceptance, no
duplicate replies, bounded amplification, and no silently consumed parameters.

It found three real defects on first use (all now fixed): duplicate error
replies, 91x reply amplification, and `-k` eating the next mode's parameter.

Note the lesson: the first version of this fuzzer reported **zero** violations
against a server that had all three. Invariants that only check "did it not
crash" find nothing. The ones that caught real bugs were the accounting ones —
*every consumed parameter must be explained*, and *every reply must be about
something the client did not already hear*.

## Options

```
STARTING
  --port N             server port                     (default 6667)
  --password P         server password                 (default simpass)
  --roster FILE        cast list                       (personas.conf)
  --users N            use only the first N personas
  --hexchat N          launch the first N personas as real HexChat GUIs
  --scenario FILE      conversation to replay          (scenario_default.conf)
  --no-scenario        connect and join, then stay quiet
  --chatter            keep generating small talk until shutdown
  --no-server          attach to an ircserv already running

DRIVING          --send NICK LINE · --cmd NICK COMMAND · --say NICK TARGET TEXT
CHECKING         --verify-names
INSPECTING       --status · --logs [NICK] · --tail NICK · --grep PAT · --server-log

SHUTDOWN         scripts/shutdown_simulation.sh [--purge] [--force]
                   --purge   also delete .sim/
                   --force   skip the polite QUIT
```

Shutdown asks every client to `QUIT` first so the transcripts end the way a
real session would, then stops the clients, then the server last — so the
server sees every client leave before it goes.

---

## Files

| File | Role |
| --- | --- |
| `../simulation.sh` | entrypoint |
| `../shutdown_simulation.sh` | teardown |
| `lib.sh` | client lifecycle, sending, process bookkeeping |
| `nc_client.sh` | one netcat client (two logs, fork-free timestamps) |
| `hexchat_profile.sh` | generates an isolated HexChat config directory |
| `addon_simctl.py` | HexChat addon: the control channel for GUI clients |
| `driver.sh` | scenario replay and chatter |
| `verify_names.sh` | naming-convention conformance probe |
| `personas.conf` | the cast |
| `scenario_default.conf` | the scripted conversation |

---

## Things that bite, and why the code looks the way it does

Each of these cost a real debugging cycle while building this. They are
commented at the site too.

**A FIFO needs a permanent writer.** A FIFO signals EOF when its *last* writer
closes. Without a holder process (`sleep infinity > in.fifo`), the first
`--send` — which opens, writes and closes — makes `nc` see EOF and exit. The
holder doubles as the shutdown lever: kill it and `nc` gets a clean EOF.

**`#` starts a comment *and* a channel name.** Stripping comments with
`${line%%#*}` or `sed 's/#.*//'` silently eats the channel column. It bit the
roster parser (everyone became a lurker) and then the scenario driver
(`PRIVMSG #general :hi` became `PRIVMSG`, answered `411`). Only whole-line
comments are stripped now.

**`cut -d' '` is not `awk`.** The scenario file is column-aligned with runs of
spaces; `cut` treats each one as its own delimiter, so field 3 came back as the
*nickname* and the server answered `421 ALICE :Unknown command`.

**HexChat 2.16 specifics**, all verified against a live server rather than
guessed:

* `hook_timer` fires **exactly once**, whatever the callback returns. The
  addon re-arms itself every tick; remove that and it reads the control fifo
  once at startup and goes deaf forever.
* `__file__` is not defined in an embedded addon — the config directory comes
  from `hexchat.get_info("configdir")`.
* A command run from a timer inherits whatever context is current, which early
  in startup is attached to nothing and the command is silently dropped. The
  addon selects the server tab explicitly.
* Autojoin needs **one `J=` line per channel**. `J=#a,#b,#c` parses, and joins
  only `#a`.
* `L=7` (LOGIN_PASS) is required for the server password. Without it HexChat
  holds the password back for a services login that never happens and every
  GUI client sits unregistered.
* HexChat rewrites its process title to a bare `hexchat`, so `pkill -f` on the
  config dir finds nothing. PIDs are tracked in `.sim/pids/`.

**Kill looping processes parent-first.** The chatter driver wins a
children-first race: kill its `sleep` and the loop spawns another before the
walk reaches the parent. `sim_kill_stubborn` SIGKILLs the parent first — a dead
shell cannot respawn — then reaps the children it snapshotted.

**A zombie is not a survivor.** A defunct process cannot be killed; reporting
one as a failed shutdown makes a clean teardown look broken. `sim_alive()`
treats state `Z` as dead.

**`printf '%s' | while read` drops the last field.** Without a trailing
newline `read` returns non-zero and the loop body never runs for it — which is
how every HexChat client lost the last channel in its list.
