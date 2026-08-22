# ai-assistant

An **agentic** IRC companion for `ircserv`, backed by the Claude API.

It connects as an ordinary IRC client (default nick `assistant`), joins the
configured channels, and answers when addressed:

- `!ai <question>`
- `assistant: <question>` (or `assistant, …` / `assistant …`)
- any direct `PRIVMSG` to the bot

Unlike a plain question-answer bot it can *act on the live server*: read a
channel's scrollback, WHO the roster, WHOIS a person, read topic and modes, post
into another channel, join and part, and — when explicitly enabled — moderate.
Web search and web fetch run server-side at Anthropic.

It is a **companion process** — not part of the C++98 `ircserv` binary or its 42
build, and it holds no privileged channel into the server. Everything it does,
it does by sending the same commands any client sends.

Full walkthrough with transcripts: [`wiki/scenarios/10-ai-assistant.md`](../../wiki/scenarios/10-ai-assistant.md).

## Run

```sh
export ANTHROPIC_API_KEY=sk-ant-...
export IRC_PASS=yourserverpassword
export IRC_CHANNELS="#general,#support"
cargo run --release
```

## Tools

| Tool | Tier | What it does |
| --- | --- | --- |
| `irc_recent_messages` | read | Channel/DM scrollback — "summarise the last hour" |
| `irc_channel_members` | read | WHO: live roster with operator flags |
| `irc_user_info` | read | WHOIS: user, host, real name, shared channels |
| `irc_channel_info` | read | Topic and active modes (`+i +t +k +l`) |
| `irc_send_message` | channel | Post into a *different* channel or to a person |
| `irc_join_channel` / `irc_part_channel` | channel | Come and go |
| `irc_set_topic` | moderation | Set a channel topic |
| `irc_kick` / `irc_invite` | moderation | Remove / let in |
| `irc_set_mode` | moderation | `+i +t +k +l +o` and their removals |
| `web_search` / `web_fetch` | — | Server-side, no local execution |

**Scrollback is deliberately not in the prompt.** It is only read when a tool
asks for it, so a routine question does not pay for 300 lines of unrelated chat,
and the cached prompt prefix stays stable.

### The moderation gate

Moderation needs **three** independent things to be true, checked per action:

1. `AI_ALLOW_MODERATION=true` on the deployment,
2. the person who asked is listed in `AI_ADMINS`,
3. that person is a **live channel operator**, re-read with WHO at the moment of
   the action — not remembered, and never taken from what someone claims in chat.

The nick list alone would be a poor gate: this server has no accounts, so a nick
is only a label whoever currently holds it. It is off by default.

## In-channel commands

Answered locally, without an API call:

| Command | Effect |
| --- | --- |
| `!ai help` | What it can do here |
| `!ai status` | Model, effort, uptime, answer/refusal/error counts, token usage |
| `!ai reset` | Forget this conversation (scrollback is kept) |
| `!ai model <id>` | Switch model at runtime — admins only |
| `!ai effort <low…max>` | Retune thinking depth — admins only |

## Environment

| Variable | Default | Notes |
| --- | --- | --- |
| `IRC_HOST` | `127.0.0.1` | ircserv host |
| `IRC_PORT` | `6667` | ircserv port |
| `IRC_PASS` | (empty) | server password |
| `IRC_NICK` | `assistant` | ≤ 9 chars; `433` collisions get a numeric suffix |
| `IRC_REALNAME` | `Claude-backed assistant` | |
| `IRC_CHANNELS` | (none) | comma-separated auto-join |
| `ANTHROPIC_API_KEY` | **required** | |
| `ANTHROPIC_MODEL` | `claude-opus-5` | |
| `ANTHROPIC_EFFORT` | `medium` | `low`/`medium`/`high`/`xhigh`/`max` |
| `ANTHROPIC_MAX_TOKENS` | `8000` | |
| `ANTHROPIC_TIMEOUT_SECS` | `180` | per HTTP request |
| `AI_PERSONA` | (none) | appended to the system prompt |
| `AI_WEB_TOOLS` | `true` | declare `web_search` / `web_fetch` |
| `AI_FALLBACKS` | `true` | `fallbacks: "default"` on a safety decline |
| `AI_SHOW_TOOLS` | `true` | NOTICE each tool call in-channel |
| `AI_SHOW_THINKING` | `false` | request and post a reasoning summary |
| `AI_LOG_LINES` | `300` | scrollback kept per target |
| `AI_HISTORY_MESSAGES` | `24` | conversation window per destination |
| `AI_MAX_ITERATIONS` | `12` | client-side tool round trips per answer |
| `AI_MAX_CONTINUATIONS` | `5` | `pause_turn` resumes per answer |
| `AI_MAX_CONCURRENT` | `3` | in-flight model calls |
| `AI_COOLDOWN_SECS` | `5` | per-nick gap between questions |
| `AI_QUERY_TIMEOUT_SECS` | `5` | wait for a numeric reply |
| `AI_ALLOW_CHANNEL_OPS` | `true` | send elsewhere, JOIN, PART |
| `AI_ALLOW_MODERATION` | `false` | TOPIC, KICK, INVITE, MODE |
| `AI_ADMINS` | (none) | comma-separated nicks |

## Design notes

- **Raw Messages API over HTTPS** (`reqwest`) — there is no official Anthropic
  Rust SDK, so this owns the agentic loop the SDK tool runners would otherwise
  own: request → `tool_use` → execute → `tool_result` → repeat.
- **Three stop reasons are handled before the text**, and skipping any is a
  silent bug: `refusal` (content is not an answer), `pause_turn` (the
  server-side web-search loop paused — resend with *no* new user message and it
  resumes), and `tool_use`.
- **Adaptive thinking** with `output_config.effort`. Assistant turns are
  appended whole, which keeps `tool_use` and thinking blocks intact across the
  loop.
- **The system prompt is byte-stable** for the process lifetime — no timestamps,
  no roster — so the cached prefix actually hits. Volatile context (`[#chan]
  <nick> text`) rides in the user message. A unit test asserts the stability.
- **A single writer task** owns the socket, so multi-second model calls never
  delay `PING`/`PONG` and the bot cannot ping-timeout mid-answer.
- **Replies are budgeted against the relayed line, not the sent one.** The
  server prepends `:nick!user@host ` and then caps at 512, so a line that is
  legal going in can be truncated coming out. The bot learns its own prefix from
  the server's echo of its own JOIN and wraps to fit.
- **A tool that fails still returns a `tool_result`** with `is_error`, never
  nothing — a dropped result strands the `tool_use` it answered.

## Tests

```sh
cargo test           # 25 unit tests: parsing, wrapping, gating, trimming, loop shapes
```

Local `cargo` must be ≥ 1.78 to read the v4 `Cargo.lock`; otherwise build in the
container the way CI does:

```sh
docker run --rm -v "$PWD":/app:ro -w /app -e CARGO_TARGET_DIR=/build rust:1-slim \
  cargo test --locked
```
