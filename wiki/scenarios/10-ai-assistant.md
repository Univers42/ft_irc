# 10 — The AI assistant (`assistant`)

**Context.** [`ircbot`](06-bot.md) is a *virtual* user living inside the server.
`assistant` is the opposite: a **separate process**, written in Rust, that dials
`ircserv` over TCP and registers like anyone else. The C++98 server contains no
code for it, no `make` tier builds it, and it gets no privileged channel — every
single thing it does, it does by sending commands any `netcat` session could
send.

What makes it worth a page of its own is that it does not merely *answer*. It
**acts on the live server**: reads a channel's scrollback, WHOs the roster,
WHOISes a person, reads topic and modes, posts into other channels, joins and
parts, and — when you explicitly turn it on — moderates. Web search runs
server-side at Anthropic.

> **Outside the 42 build.** Nothing here is on the evaluation sheet. `make
> mandatory`, `make bonus` and `make` are all unaware of it; `cargo` builds it
> separately. Defend on `make mandatory` — this is for the "what else did you
> build" conversation.

> **Transcript convention.** Every transcript below that the *bot itself*
> produces locally is captured from a live `./ircserv` + `ai-assistant` pair.
> The exchanges marked **illustrative** show the shape of a model-driven answer:
> those need a funded API key and are non-deterministic, so they are not
> reproducible line-for-line the way the rest of this wiki is.

---

## Turning it on

You need an Anthropic API key. Put it in `.env`, which is gitignored.

```bash
cp .env.example .env         # set ANTHROPIC_API_KEY
docker compose up --build    # ircserv + ai-assistant
```

Or run it against a server you already have up:

```bash
export ANTHROPIC_API_KEY=sk-ant-...
export IRC_PASS=mypass IRC_CHANNELS='#general'
cd companions/ai-assistant && cargo run --release
```

It joins, and appears in the member list like anyone else — because it *is*
anyone else. Here it created `#general`, so the server gave it `@`:

```
:ft_irc 353 alice = #general :@assistant alice
:ft_irc 352 alice #general assistant 127.0.0.1 ft_irc assistant H@ :0 Claude-backed assistant
```

Compare with `ircbot`, which appears in **no** member list and **no** `WHO`
output, because it holds no connection at all.

---

## HexChat

Anywhere it has joined:

```
!ai what is a channel key for?
assistant: summarise the last hour
assistant, who is in here right now?
```

Or open a query — `/msg assistant hello` — where **every** line is for it and
you do not need to prefix anything.

---

## netcat

```
PRIVMSG #general :!ai help
```

```
:assistant!assistant@127.0.0.1 PRIVMSG #general :alice: ask me anything — "!ai <question>", "assistant: <question>", or a private message. I can read channel scrollback, WHO/WHOIS, topics and modes, and search the web. Admin: !ai status · !ai reset · !ai model <id> · !ai effort <low|medium|high|xhigh|max>
```

Three address forms, and one deliberate non-match:

| You type | Result |
| --- | --- |
| `!ai <question>` | addressed |
| `assistant: …` · `assistant, …` · `assistant …` | addressed, case-insensitive |
| any line in a `/msg` query | addressed |
| `!aid me with this` | **not** addressed — the trigger must end at a separator |
| `assistantbot: hi` | **not** addressed — same rule, so a longer nick can't be hijacked |

---

## What makes it agentic

A plain bot answers from the words in front of it. This one decides it needs
something, goes and gets it from the server, then answers. Ask *"who's here and
who can kick?"* and it does not guess from scrollback — it sends a real `WHO`
and reads the operator flags out of the `352` replies.

The tools it can reach for:

| Tool | Tier | Underlying IRC |
| --- | --- | --- |
| `irc_recent_messages` | read | (local scrollback buffer) |
| `irc_channel_members` | read | `WHO #chan` → `352` / `315` |
| `irc_user_info` | read | `WHOIS nick` → `311` / `319` / `318` |
| `irc_channel_info` | read | `TOPIC #chan` → `332`/`331`, `MODE #chan` → `324` |
| `irc_send_message` | channel | `PRIVMSG` elsewhere |
| `irc_join_channel` · `irc_part_channel` | channel | `JOIN` / `PART` |
| `irc_set_topic` | moderation | `TOPIC #chan :text` |
| `irc_kick` · `irc_invite` | moderation | `KICK` / `INVITE` |
| `irc_set_mode` | moderation | `MODE #chan +o nick` |
| `web_search` · `web_fetch` | — | server-side at Anthropic, no local execution |

With `AI_SHOW_TOOLS=true` (the default) each call is announced in-channel as a
`NOTICE`, so nothing happens invisibly:

```
· irc_channel_members #general
```

### A worked example — *illustrative*

```
<alice> assistant: who's here, and who can kick?
-assistant- · irc_channel_members #general
<assistant> alice: five of you. bob and carol are operators, so they can kick — you, dave and I can't.
```

What actually happened on the wire between those two lines:

1. The model answered with a `tool_use` block instead of text.
2. The bot sent `WHO #general` and registered interest in `352` and `315`.
3. The read loop collected the `352` member lines until `315` closed the burst.
4. The roster went back as a `tool_result`, and the model wrote the sentence.

Step 3 is the part that needed real plumbing. The tool runs in a spawned task,
but its reply arrives on the same read loop that is servicing `PING`, so the
tool registers a waiter and the read loop completes it when the terminating
numeric shows up. Everything is bounded by a timeout, because two of these
bursts — `324` `RPL_CHANNELMODEIS` and `332` `RPL_TOPIC` — have **no**
terminating numeric at all.

### Scrollback

It records every channel line it sees, whether addressed or not, which is what
makes this answerable:

```
<alice> assistant: catch me up, I was away
-assistant- · irc_recent_messages #general
<assistant> alice: bob hit a segfault in the mode parser, carol found it was the +l bound, and they merged a fix twenty minutes ago.
```

*(illustrative)*

That scrollback is **not** in the prompt. It is only read when the model asks
for it, so an unrelated question does not pay for 300 lines of chat, and the
cached prompt prefix stays byte-stable. It only covers what happened while the
bot was connected and present — it is not server history, and the bot says so
rather than inventing it.

---

## Moderation, and why it is off

Turn it on and the bot can kick people. That is a real power to hand to
something that acts on chat text, so it is gated three ways, **checked on every
single action**:

```bash
AI_ALLOW_MODERATION=true
AI_ADMINS=alice,bob
```

1. The deployment has `AI_ALLOW_MODERATION=true`.
2. The person who asked is in `AI_ADMINS`.
3. That person is a **live channel operator** — re-read with `WHO` at the moment
   of the action.

Check 3 is the one that matters. This server has no accounts and no SASL, so a
nick is only a label whoever currently holds it — an `AI_ADMINS` list on its own
would hand the keys to whoever claims the name next. And the check is against
**who asked**, never against a nick the model supplies, so nobody can talk the
bot into acting "as" an operator by saying they are one.

If a gate refuses, the refusal goes back to the model as a failed tool result
telling it to report the refusal plainly and *not* look for another route.

A moderation action is confirmed by the server's own broadcast echo — the bot is
in the channel, so it sees its own `KICK` come back. No echo and no error inside
the timeout, and it says it cannot tell rather than claiming success.

---

## In-channel commands

Answered locally. No API call, no token spend, no cooldown.

```
PRIVMSG #general :!ai status
```

```
:assistant!assistant@127.0.0.1 PRIVMSG #general :model=claude-opus-5 effort=medium · up 0h00m · answers/refusals/errors 0/0/0 · tokens in 0 out 0 (cache read 0) · tools 13 · web on · moderation on
```

`tools 13` is the live count for that gating: 4 read + 3 channel + 4 moderation
+ 2 web. With moderation off it reads `tools 9`.

| Command | Effect |
| --- | --- |
| `!ai help` | what it can do *here*, reflecting the gates |
| `!ai status` | model, effort, uptime, counters, token usage, tool count |
| `!ai reset` | forget this conversation (scrollback is kept) |
| `!ai model <id>` | switch model at runtime — admins only |
| `!ai effort <low…max>` | retune thinking depth — admins only |

Effort is the cost/latency dial, and it is validated before it can reach the API:

```
:assistant!assistant@127.0.0.1 PRIVMSG #general :effort=medium
:assistant!assistant@127.0.0.1 PRIVMSG #general :alice: effort set to xhigh.
:assistant!assistant@127.0.0.1 PRIVMSG #general :alice: effort must be one of low, medium, high, xhigh, max.
```

---

## Back pressure

A model call takes seconds and costs money, so questions are throttled per nick
(`AI_COOLDOWN_SECS`, default 5) and concurrent calls are capped
(`AI_MAX_CONCURRENT`, default 3). The brush-off is a `NOTICE` to the asker
alone, not channel noise:

```
:assistant!assistant@127.0.0.1 NOTICE alice :Easy — 5s before your next question.
```

When the API is unreachable or the key is wrong, it degrades to one line and
keeps serving IRC — it does not drop off the server:

```
:assistant!assistant@127.0.0.1 PRIVMSG #general :alice: the assistant is unavailable right now.
```

That failure is also visible in `!ai status`, which is the fastest way to tell
"the bot is wedged" from "the bot is fine and your key is not":

```
:assistant!assistant@127.0.0.1 PRIVMSG #general :model=claude-opus-5 effort=xhigh · up 0h00m · answers/refusals/errors 0/0/1 · tokens in 0 out 0 (cache read 0) · tools 13 · web on · moderation on
```

---

## Three things it gets right about *this* server

The companion is written against `ircserv`'s actual behaviour, not against IRC
in the abstract. Three places where that shows:

**Replies are budgeted against the relayed line, not the sent one.** The server
re-frames a client's line with `:nick!user@host ` before handing it to members,
*then* caps the result at 512 bytes — so a line that is perfectly legal going in
can be truncated coming out. The bot learns its own prefix from the server's
echo of its own `JOIN` (the only place that prefix appears) and wraps to fit,
on word boundaries.

**Nicks are 9 characters and truncated, not rejected.** `assistant` is exactly
9. On a `433` collision it retries with a numeric suffix computed against the
`NICKLEN` it read out of `005`, rather than assuming.

**Casemapping is ASCII.** `#General` and `#general` are one channel, and the bot
folds the same way the server does — deliberately *not* Unicode-aware, because
folding accented letters would make two distinct nicks compare equal.

It also strips `\r`, `\n` and `\0` from everything it sends. A model that echoes
attacker-supplied text is a line-injection vector otherwise; `\x01` (CTCP) is
left alone, because the server passes it through.

---

## How it talks to Claude

There is no official Anthropic SDK for Rust, so the companion speaks the
Messages API over raw HTTPS and owns the agentic loop itself: request →
`tool_use` → execute → `tool_result` → repeat, until the model stops calling
tools. `AI_MAX_ITERATIONS` bounds it.

Three response shapes are handled *before* the text, and skipping any one is a
silent bug:

| `stop_reason` | Meaning | What the bot does |
| --- | --- | --- |
| `refusal` | a safety classifier declined | say so — `content` is **not** an answer |
| `pause_turn` | the server-side web-search loop paused | resend with **no** new user message; it resumes |
| `tool_use` | our turn | run the tools, send all results in one message |

Other choices worth knowing: the system prompt is byte-stable for the process
lifetime (no timestamps, no roster) so the cached prefix actually hits — a unit
test asserts it; a single writer task owns the socket so a multi-second model
call can never delay `PONG` and ping-timeout the bot mid-answer; and a
conversation is persisted only when a turn *succeeds*, so a failed turn cannot
leave a dangling `tool_use` in the stored history.

---

## Checks

* `!ai help` and `!ai status` answer with **no** API key set — they are local.
* `!aid me` and `assistantbot: hi` get no reply; `!ai`, `assistant: x`,
  `Assistant, x` and a `/msg` all do.
* `!ai status` reports `tools 9` with moderation off, `tools 13` with it on.
* A non-admin gets refused by `!ai model` / `!ai effort`.
* Two questions in a row: the second gets the cooldown `NOTICE`.
* With a bad key, the bot stays connected and answers `WHO` normally.
* `assistant` appears in `353` and `352`; `ircbot` appears in neither.

```bash
cd companions/ai-assistant && cargo test        # 25 unit tests
```

Covering address-form matching, line wrapping against the relayed budget, the
capability gating, conversation trimming that cannot orphan a `tool_result`, and
system-prompt prefix stability. Local `cargo` must be ≥ 1.78 to read the v4
lockfile — otherwise build it the way CI does:

```bash
docker run --rm -v "$PWD":/app:ro -w /app -e CARGO_TARGET_DIR=/build \
  rust:1-slim cargo test --locked
```

---

## Limits

* **It only knows what it saw.** Scrollback starts when it connects. It is not
  server history and there is no persistence across restarts.
* **Conversations are per destination**, capped at `AI_HISTORY_MESSAGES`, and
  lost on restart.
* **`WHOIS` channel lists can truncate.** `RPL_WHOISCHANNELS` is not chunked by
  the server — a user in very many channels gets a clipped `319`.
* **Nick-based admin is weak** on a server with no accounts. That is exactly why
  moderation also demands live operator status.
* **It costs money per answer.** `!ai status` shows the running token usage.

---

**See also:** [06 — The bot](06-bot.md) for `ircbot`, the in-server virtual user
· [09 — Platform extras](09-platform-extras.md) for the audit trail, the
platform bus and the rest of the Docker stack ·
[`companions/ai-assistant/README.md`](../../companions/ai-assistant/README.md)
for the full environment reference.
