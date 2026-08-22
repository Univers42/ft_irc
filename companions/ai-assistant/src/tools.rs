//! The bot's hands: client-side tools that act on `ircserv` through ordinary
//! IRC commands, plus the server-side web tools it just declares.
//!
//! Everything here goes over the wire as a normal client would send it. The
//! bot has no privileged channel into the server — if it can't do something as
//! a client, it can't do it at all.
//!
//! ## Three capability tiers
//!
//! | Tier | Gate | Tools |
//! | --- | --- | --- |
//! | Read | always | scrollback, WHO, WHOIS, TOPIC/MODE queries |
//! | Channel | `AI_ALLOW_CHANNEL_OPS` (default on) | send elsewhere, JOIN, PART |
//! | Moderation | `AI_ALLOW_MODERATION` (default **off**) | TOPIC set, KICK, INVITE, MODE |
//!
//! A moderation tool needs three independent things to be true: the tier is
//! enabled, the person who asked is in `AI_ADMINS`, and that person is *live*
//! channel operator (verified with a WHO, not remembered). The nick list alone
//! would be a poor gate — this server has no accounts, so a nick is only a
//! label someone currently holds.

use std::sync::Arc;

use serde_json::{json, Value};
use tokio::sync::Mutex;

use crate::config::Config;
use crate::irc::{irc_eq, Irc, Line, QuerySpec, Rule};
use crate::memory::Memory;

/// Cap on proactive `irc_send_message` calls inside a single answer, so a
/// confused loop cannot turn the bot into a spammer.
const MAX_PROACTIVE_SENDS: usize = 3;

// ── Reply shapes ────────────────────────────────────────────────────────────
//
// `at: Some(i)` where the position is known; `Rule::err` (any parameter) for
// the error numerics, whose subject position is not consistent — 482 puts the
// channel at 1, 441 puts it at 2.

const WHO: &[Rule] = &[Rule::collect("352", 1), Rule::finish("315", 1)];

const WHOIS: &[Rule] = &[
    Rule::collect("311", 1),
    Rule::collect("312", 1),
    Rule::collect("319", 1),
    Rule::finish("318", 1),
    Rule::finish("401", 1),
];

// 332 is always followed by 333, so 333 terminates; 331 stands alone.
const TOPIC_READ: &[Rule] = &[
    Rule::collect("332", 1),
    Rule::finish("333", 1),
    Rule::finish("331", 1),
    Rule::err("403"),
    Rule::err("442"),
];

// 324 is always followed by 329 (creation time).
const MODE_READ: &[Rule] = &[
    Rule::collect("324", 1),
    Rule::finish("329", 1),
    Rule::err("403"),
    Rule::err("442"),
];

// Write confirmations: the server broadcasts the command back to the channel,
// and we are a member, so our own echo is the success signal.
const JOIN_ACK: &[Rule] = &[
    Rule::finish("JOIN", 0),
    Rule::err("403"),
    Rule::err("405"),
    Rule::err("471"),
    Rule::err("473"),
    Rule::err("475"),
    Rule::err("476"),
];

const PART_ACK: &[Rule] = &[Rule::finish("PART", 0), Rule::err("403"), Rule::err("442")];

const TOPIC_ACK: &[Rule] = &[
    Rule::finish("TOPIC", 0),
    Rule::err("403"),
    Rule::err("442"),
    Rule::err("461"),
    Rule::err("482"),
];

const KICK_ACK: &[Rule] = &[
    Rule::finish("KICK", 0),
    Rule::err("401"),
    Rule::err("403"),
    Rule::err("441"),
    Rule::err("442"),
    Rule::err("461"),
    Rule::err("482"),
];

const MODE_ACK: &[Rule] = &[
    Rule::finish("MODE", 0),
    Rule::err("401"),
    Rule::err("403"),
    Rule::err("441"),
    Rule::err("442"),
    Rule::err("461"),
    Rule::err("472"),
    Rule::err("482"),
    Rule::err("525"),
    Rule::err("696"),
];

// INVITE is acknowledged with 341 `<me> <nick> <channel>` — channel at 2 — and
// the invited user gets the INVITE, not us.
const INVITE_ACK: &[Rule] = &[
    Rule { command: "341", at: None, terminal: true },
    Rule::err("401"),
    Rule::err("403"),
    Rule::err("442"),
    Rule::err("443"),
    Rule::err("461"),
    Rule::err("482"),
];

// ─────────────────────────────────────────────────────────────────────────────
//  Catalog
// ─────────────────────────────────────────────────────────────────────────────

/// Build the `tools` array for a request.
///
/// Descriptions are prescriptive about *when* to call, not just what the tool
/// does — recent Opus models reach for tools conservatively, and a trigger
/// condition in the description is what moves the should-call rate.
pub fn catalog(cfg: &Config) -> Vec<Value> {
    let mut tools = vec![
        json!({
            "name": "irc_recent_messages",
            "description": "Read the recent chat scrollback of a channel or a person. \
                Call this whenever the question refers to what was said earlier — \
                'summarise the last hour', 'what did bob decide', 'catch me up', \
                'who was arguing about X'. You only see messages sent while you were \
                connected and present. Returns oldest-first.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "target": {"type": "string", "description": "Channel (#name) or nick. Defaults to the conversation you are replying in."},
                    "limit": {"type": "integer", "description": "How many lines back, 1-200. Default 40."}
                }
            }
        }),
        json!({
            "name": "irc_channel_members",
            "description": "List who is currently in a channel and which of them are \
                channel operators. Call this before answering 'who is here', 'who can \
                kick', 'is alice online', or before any moderation action, so you act \
                on the live roster rather than on the scrollback.",
            "input_schema": {
                "type": "object",
                "properties": {"channel": {"type": "string", "description": "Channel name including the leading #"}},
                "required": ["channel"]
            }
        }),
        json!({
            "name": "irc_user_info",
            "description": "WHOIS one user: their username, host, real name and the \
                channels they share with you. Call this when asked about a specific \
                person's identity or presence.",
            "input_schema": {
                "type": "object",
                "properties": {"nick": {"type": "string"}},
                "required": ["nick"]
            }
        }),
        json!({
            "name": "irc_channel_info",
            "description": "Read a channel's topic and its active modes (+i invite-only, \
                +t topic locked to operators, +k key, +l member limit). Call this when \
                asked what a channel is for, why someone cannot join, or before \
                changing a mode.",
            "input_schema": {
                "type": "object",
                "properties": {"channel": {"type": "string"}},
                "required": ["channel"]
            }
        }),
    ];

    if cfg.allow_channel_ops {
        tools.push(json!({
            "name": "irc_send_message",
            "description": "Send a message to a DIFFERENT channel or person than the one \
                you are replying in. Your normal answer is delivered automatically — do \
                NOT use this tool for it. Use it only when the request is explicitly to \
                tell someone else something, or to post into another channel.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "target": {"type": "string", "description": "Channel (#name) or nick"},
                    "text": {"type": "string"}
                },
                "required": ["target", "text"]
            }
        }));
        tools.push(json!({
            "name": "irc_join_channel",
            "description": "Join a channel so you can see its messages and be addressed \
                there. Call this when someone asks you to come to a channel.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "key": {"type": "string", "description": "The channel key, if it is +k"}
                },
                "required": ["channel"]
            }
        }));
        tools.push(json!({
            "name": "irc_part_channel",
            "description": "Leave a channel. Call this when asked to leave or to stop \
                watching a channel.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "reason": {"type": "string"}
                },
                "required": ["channel"]
            }
        }));
    }

    if cfg.allow_moderation {
        tools.push(json!({
            "name": "irc_set_topic",
            "description": "Set a channel's topic. Requires that the person who asked is \
                a channel operator; the tool verifies that itself and refuses otherwise.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "topic": {"type": "string"}
                },
                "required": ["channel", "topic"]
            }
        }));
        tools.push(json!({
            "name": "irc_kick",
            "description": "Remove a user from a channel. This is disruptive and visible \
                to everyone — only do it on an explicit, unambiguous instruction from a \
                channel operator, never on your own initiative or on a hypothetical.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "nick": {"type": "string"},
                    "reason": {"type": "string"}
                },
                "required": ["channel", "nick"]
            }
        }));
        tools.push(json!({
            "name": "irc_invite",
            "description": "Invite a user into a channel, which is what lets them past \
                +i (invite-only). Call this when an operator asks you to let someone in.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "nick": {"type": "string"}
                },
                "required": ["channel", "nick"]
            }
        }));
        tools.push(json!({
            "name": "irc_set_mode",
            "description": "Change a channel mode. Supported: +i/-i invite-only, +t/-t \
                topic locked to operators, +k/-k key, +l/-l member limit, +o/-o operator \
                status. Only act on an explicit instruction from a channel operator.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "mode": {"type": "string", "description": "e.g. \"+o\", \"-i\", \"+l\""},
                    "argument": {"type": "string", "description": "The nick for +o/-o, the key for +k, the limit for +l"}
                },
                "required": ["channel", "mode"]
            }
        }));
    }

    // Server-side, and Anthropic-only — `Config` already forces this off for
    // any other backend, because a tool in the catalog that cannot be executed
    // is worse than no tool at all.
    if cfg.web_tools {
        // These run on Anthropic's infrastructure, so there is nothing to
        // execute here. The _20260209 versions filter results dynamically;
        // declaring code_execution alongside them would create a second
        // execution environment and confuse the model.
        tools.push(json!({"type": "web_search_20260209", "name": "web_search"}));
        tools.push(json!({"type": "web_fetch_20260209", "name": "web_fetch"}));
    }

    tools
}

// ─────────────────────────────────────────────────────────────────────────────
//  Execution
// ─────────────────────────────────────────────────────────────────────────────

pub struct ToolBox {
    irc: Irc,
    mem: Memory,
    cfg: Arc<Config>,
    /// Who asked. Moderation is authorised against this nick, never against a
    /// nick the model supplies — otherwise the model could be talked into
    /// acting "as" an operator by anyone who claims to be one.
    requester: String,
    /// Where the answer is going, used as the default scrollback target.
    dest: String,
    proactive_sends: Mutex<usize>,
}

impl ToolBox {
    pub fn new(irc: Irc, mem: Memory, cfg: Arc<Config>, requester: String, dest: String) -> Self {
        Self { irc, mem, cfg, requester, dest, proactive_sends: Mutex::new(0) }
    }

    /// Run one tool call. The `bool` is `is_error`, which the caller puts on the
    /// `tool_result` block — a refused or failed tool must still come back as a
    /// result, never be dropped.
    pub async fn execute(&self, name: &str, input: &Value) -> (String, bool) {
        if self.cfg.show_tools {
            self.irc.notice(&self.dest, &format!("· {}", describe(name, input))).await;
        }

        let result = match name {
            "irc_recent_messages" => self.recent_messages(input).await,
            "irc_channel_members" => self.channel_members(input).await,
            "irc_user_info" => self.user_info(input).await,
            "irc_channel_info" => self.channel_info(input).await,
            "irc_send_message" => self.send_message(input).await,
            "irc_join_channel" => self.join_channel(input).await,
            "irc_part_channel" => self.part_channel(input).await,
            "irc_set_topic" => self.set_topic(input).await,
            "irc_kick" => self.kick(input).await,
            "irc_invite" => self.invite(input).await,
            "irc_set_mode" => self.set_mode(input).await,
            other => Err(format!("no such tool: {other}")),
        };

        match result {
            Ok(text) => (text, false),
            Err(text) => (text, true),
        }
    }

    // ── Read tier ───────────────────────────────────────────────────────────

    async fn recent_messages(&self, input: &Value) -> Result<String, String> {
        let target = str_arg(input, "target").unwrap_or_else(|| self.dest.clone());
        let limit = input
            .get("limit")
            .and_then(Value::as_u64)
            .unwrap_or(40)
            .clamp(1, 200) as usize;

        let entries = self.mem.recent(&target, limit).await;
        if entries.is_empty() {
            return Ok(format!(
                "No scrollback for {target}. Either nothing was said since you connected, \
                 or you are not in that channel."
            ));
        }

        let lines: Vec<String> = entries
            .iter()
            .map(|e| format!("[{}] <{}> {}", clock(e.at), e.nick, e.text))
            .collect();
        Ok(format!(
            "{} of the most recent lines in {target}, oldest first:\n{}",
            lines.len(),
            lines.join("\n")
        ))
    }

    async fn channel_members(&self, input: &Value) -> Result<String, String> {
        let channel = require(input, "channel")?;
        let members = self.who(&channel).await;
        if members.is_empty() {
            return Ok(format!(
                "{channel} has no visible members — it does not exist, or you are not in it."
            ));
        }
        let rendered: Vec<String> = members
            .iter()
            .map(|m| {
                if m.operator {
                    format!("{} (operator)", m.nick)
                } else {
                    m.nick.clone()
                }
            })
            .collect();
        Ok(format!(
            "{} members in {channel}: {}",
            rendered.len(),
            rendered.join(", ")
        ))
    }

    async fn user_info(&self, input: &Value) -> Result<String, String> {
        let nick = require(input, "nick")?;
        let lines = self
            .irc
            .query(format!("WHOIS {nick}"), spec(WHOIS, &nick))
            .await;

        if lines.iter().any(|l| l.command == "401") {
            return Ok(format!("No user called {nick} is connected."));
        }

        let mut out = Vec::new();
        for l in &lines {
            match l.command.as_str() {
                // 311 <me> <nick> <user> <host> * :<realname>
                "311" => out.push(format!(
                    "{} is {}@{} ({})",
                    l.param(1),
                    l.param(2),
                    l.param(3),
                    l.param(5)
                )),
                // 319 <me> <nick> :<channels>
                "319" => out.push(format!("channels: {}", l.param(2))),
                _ => {}
            }
        }
        if out.is_empty() {
            return Ok(format!("The server did not answer WHOIS for {nick} in time."));
        }
        Ok(out.join("; "))
    }

    async fn channel_info(&self, input: &Value) -> Result<String, String> {
        let channel = require(input, "channel")?;

        let topic_lines = self
            .irc
            .query(format!("TOPIC {channel}"), spec(TOPIC_READ, &channel))
            .await;
        let topic = topic_lines
            .iter()
            .find(|l| l.command == "332")
            .map(|l| format!("topic: {}", l.param(2)))
            .or_else(|| {
                topic_lines
                    .iter()
                    .find(|l| l.command == "331")
                    .map(|_| "topic: (none set)".to_string())
            });

        let mode_lines = self
            .irc
            .query(format!("MODE {channel}"), spec(MODE_READ, &channel))
            .await;
        // 324 <me> <channel> <modes> [params...]
        let modes = mode_lines.iter().find(|l| l.command == "324").map(|l| {
            let rest: Vec<&str> = l.params[2..].iter().map(String::as_str).collect();
            format!("modes: {}", rest.join(" "))
        });

        match (topic, modes) {
            (None, None) => Ok(format!(
                "Could not read {channel}. It may not exist, or you are not in it — \
                 this server requires membership to read a channel's modes, because \
                 the reply carries the +k key."
            )),
            (t, m) => Ok([t, m].into_iter().flatten().collect::<Vec<_>>().join("; ")),
        }
    }

    // ── Channel tier ────────────────────────────────────────────────────────

    async fn send_message(&self, input: &Value) -> Result<String, String> {
        self.require_tier(self.cfg.allow_channel_ops, "AI_ALLOW_CHANNEL_OPS")?;
        let target = require(input, "target")?;
        let text = require(input, "text")?;

        if irc_eq(&target, &self.irc.nick().await) {
            return Err("Refusing to message yourself.".into());
        }
        if irc_eq(&target, &self.dest) {
            return Err(format!(
                "{target} is the conversation you are already replying in — your answer \
                 is delivered there automatically. Do not use this tool for it."
            ));
        }
        if target.starts_with('#') && !self.irc.in_channel(&target).await {
            return Err(format!(
                "Not in {target}, so you cannot post there. Join it first if that was asked for."
            ));
        }

        {
            let mut n = self.proactive_sends.lock().await;
            if *n >= MAX_PROACTIVE_SENDS {
                return Err(format!(
                    "Already sent {MAX_PROACTIVE_SENDS} messages elsewhere in this answer; \
                     that is the cap. Say the rest in your reply instead."
                ));
            }
            *n += 1;
        }

        self.irc.privmsg(&target, &text).await;
        // The server does not echo our own PRIVMSG back to us, so record it
        // ourselves or it is missing from the scrollback the bot can read.
        let me = self.irc.nick().await;
        self.mem.record(&target, &me, &text).await;
        Ok(format!("Sent to {target}."))
    }

    async fn join_channel(&self, input: &Value) -> Result<String, String> {
        self.require_tier(self.cfg.allow_channel_ops, "AI_ALLOW_CHANNEL_OPS")?;
        let channel = require(input, "channel")?;
        if self.irc.in_channel(&channel).await {
            return Ok(format!("Already in {channel}."));
        }

        let key = str_arg(input, "key");
        let request = match &key {
            Some(k) => format!("JOIN {channel} {k}"),
            None => format!("JOIN {channel}"),
        };
        let lines = self.irc.query(request, spec(JOIN_ACK, &channel)).await;
        self.report(&lines, "JOIN", &channel, &format!("Joined {channel}."))
    }

    async fn part_channel(&self, input: &Value) -> Result<String, String> {
        self.require_tier(self.cfg.allow_channel_ops, "AI_ALLOW_CHANNEL_OPS")?;
        let channel = require(input, "channel")?;
        let reason = str_arg(input, "reason").unwrap_or_else(|| "asked to leave".into());
        let lines = self
            .irc
            .query(format!("PART {channel} :{reason}"), spec(PART_ACK, &channel))
            .await;
        self.report(&lines, "PART", &channel, &format!("Left {channel}."))
    }

    // ── Moderation tier ─────────────────────────────────────────────────────

    async fn set_topic(&self, input: &Value) -> Result<String, String> {
        let channel = require(input, "channel")?;
        let topic = require(input, "topic")?;
        self.require_moderator(&channel).await?;
        let lines = self
            .irc
            .query(format!("TOPIC {channel} :{topic}"), spec(TOPIC_ACK, &channel))
            .await;
        self.report(&lines, "TOPIC", &channel, &format!("Topic of {channel} set."))
    }

    async fn kick(&self, input: &Value) -> Result<String, String> {
        let channel = require(input, "channel")?;
        let nick = require(input, "nick")?;
        self.require_moderator(&channel).await?;

        if irc_eq(&nick, &self.irc.nick().await) {
            return Err("Refusing to kick yourself.".into());
        }
        let reason = str_arg(input, "reason")
            .unwrap_or_else(|| format!("requested by {}", self.requester));
        let lines = self
            .irc
            .query(
                format!("KICK {channel} {nick} :{reason}"),
                spec(KICK_ACK, &channel),
            )
            .await;
        self.report(&lines, "KICK", &channel, &format!("Kicked {nick} from {channel}."))
    }

    async fn invite(&self, input: &Value) -> Result<String, String> {
        let channel = require(input, "channel")?;
        let nick = require(input, "nick")?;
        self.require_moderator(&channel).await?;
        let lines = self
            .irc
            .query(format!("INVITE {nick} {channel}"), spec(INVITE_ACK, &channel))
            .await;
        self.report(&lines, "341", &channel, &format!("Invited {nick} to {channel}."))
    }

    async fn set_mode(&self, input: &Value) -> Result<String, String> {
        let channel = require(input, "channel")?;
        let mode = require(input, "mode")?;
        self.require_moderator(&channel).await?;

        let request = match str_arg(input, "argument") {
            Some(arg) => format!("MODE {channel} {mode} {arg}"),
            None => format!("MODE {channel} {mode}"),
        };
        let lines = self.irc.query(request, spec(MODE_ACK, &channel)).await;
        self.report(&lines, "MODE", &channel, &format!("Set {mode} on {channel}."))
    }

    // ── Gates and shared helpers ────────────────────────────────────────────

    fn require_tier(&self, enabled: bool, var: &str) -> Result<(), String> {
        if enabled {
            Ok(())
        } else {
            Err(format!(
                "That tool is disabled on this deployment ({var} is off). Say so plainly \
                 rather than trying another tool to work around it."
            ))
        }
    }

    /// The three-part moderation gate. Deliberately checks the *requester*, and
    /// re-reads operator status from the server every time.
    async fn require_moderator(&self, channel: &str) -> Result<(), String> {
        self.require_tier(self.cfg.allow_moderation, "AI_ALLOW_MODERATION")?;

        if !self.cfg.is_admin(&self.requester) {
            return Err(format!(
                "{} is not in the bot's admin list, so moderation is refused. Tell them \
                 to ask an admin — do not try another route.",
                self.requester
            ));
        }

        let members = self.who(channel).await;
        if members.is_empty() {
            return Err(format!(
                "Cannot verify operator status in {channel}: the WHO returned nothing. \
                 Refusing the action."
            ));
        }
        let is_op = members
            .iter()
            .any(|m| irc_eq(&m.nick, &self.requester) && m.operator);
        if !is_op {
            return Err(format!(
                "{} is not a channel operator in {channel} right now, so this is refused.",
                self.requester
            ));
        }
        Ok(())
    }

    async fn who(&self, channel: &str) -> Vec<Member> {
        self.irc
            .query(format!("WHO {channel}"), spec(WHO, channel))
            .await
            .iter()
            .filter(|l| l.command == "352")
            .map(Member::from_who)
            .collect()
    }

    /// Turn a confirmation burst into a sentence: the echo means it worked, an
    /// error numeric means it didn't, and silence means we cannot tell.
    fn report(
        &self,
        lines: &[Line],
        echo: &str,
        channel: &str,
        success: &str,
    ) -> Result<String, String> {
        if let Some(err) = lines.iter().find(|l| l.command.chars().all(|c| c.is_ascii_digit()) && l.command != "341") {
            let text = err.params.last().map(String::as_str).unwrap_or("rejected");
            return Err(format!("The server refused it: {text} ({}).", err.command));
        }
        if lines.iter().any(|l| irc_eq(&l.command, echo)) {
            return Ok(success.to_string());
        }
        Err(format!(
            "No confirmation from the server for {channel} within the timeout, so it is \
             unclear whether it took effect. Do not claim it succeeded."
        ))
    }
}

struct Member {
    nick: String,
    operator: bool,
}

impl Member {
    /// `352 <me> <channel> <user> <host> <server> <nick> <flags> :<hops> <real>`
    fn from_who(l: &Line) -> Self {
        Self {
            nick: l.param(5).to_string(),
            operator: l.param(6).contains('@'),
        }
    }
}

fn spec(rules: &'static [Rule], arg: &str) -> QuerySpec {
    QuerySpec { rules, arg: arg.to_string() }
}

fn str_arg(input: &Value, key: &str) -> Option<String> {
    input
        .get(key)
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(String::from)
}

fn require(input: &Value, key: &str) -> Result<String, String> {
    str_arg(input, key).ok_or_else(|| format!("the `{key}` parameter is required and was empty"))
}

/// HH:MM:SS in UTC, without pulling in a date crate for it.
fn clock(at: std::time::SystemTime) -> String {
    let secs = at
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let day = secs % 86_400;
    format!("{:02}:{:02}:{:02}", day / 3600, (day % 3600) / 60, day % 60)
}

/// The one-line "· doing X" the channel sees while a tool runs.
fn describe(name: &str, input: &Value) -> String {
    let arg = ["channel", "target", "nick", "query"]
        .iter()
        .find_map(|k| str_arg(input, k))
        .unwrap_or_default();
    match (name, arg.is_empty()) {
        (n, true) => n.to_string(),
        (n, false) => format!("{n} {arg}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::irc::parse;

    fn cfg(moderation: bool, channel_ops: bool, web: bool) -> Config {
        let mut c = base_config();
        c.allow_moderation = moderation;
        c.allow_channel_ops = channel_ops;
        c.web_tools = web;
        c
    }

    fn base_config() -> Config {
        Config {
            host: "h".into(), port: 1, pass: String::new(), nick: "assistant".into(),
            realname: "r".into(), channels: vec![], api_key: "k".into(),
            backend: crate::llm::Backend::Anthropic,
            base_url: "http://x/v1/messages".into(), temperature: 0.2,
            model: "claude-opus-5".into(), effort: "medium".into(), max_tokens: 100,
            api_timeout: std::time::Duration::from_secs(1), persona: None,
            history_messages: 10, log_lines: 10, max_iterations: 3, max_continuations: 1,
            max_concurrent: 1, cooldown: std::time::Duration::from_secs(0),
            query_timeout: std::time::Duration::from_secs(1), show_tools: false,
            show_thinking: false, web_tools: true, fallbacks: true,
            allow_channel_ops: true, allow_moderation: false, admins: vec!["alice".into()],
        }
    }

    fn names(tools: &[Value]) -> Vec<String> {
        tools
            .iter()
            .map(|t| t["name"].as_str().unwrap_or_default().to_string())
            .collect()
    }

    #[test]
    fn moderation_tools_are_absent_unless_enabled() {
        let off = names(&catalog(&cfg(false, true, false)));
        assert!(!off.contains(&"irc_kick".to_string()));
        let on = names(&catalog(&cfg(true, true, false)));
        assert!(on.contains(&"irc_kick".to_string()));
        assert!(on.contains(&"irc_set_mode".to_string()));
    }

    #[test]
    fn channel_tier_gates_the_write_tools() {
        let off = names(&catalog(&cfg(false, false, false)));
        assert!(!off.contains(&"irc_join_channel".to_string()));
        // Read tools survive every gate.
        assert!(off.contains(&"irc_recent_messages".to_string()));
        assert!(off.contains(&"irc_channel_members".to_string()));
    }

    #[test]
    fn web_tools_are_declared_with_the_dynamic_filtering_versions() {
        let tools = catalog(&cfg(false, false, true));
        let types: Vec<&str> = tools.iter().filter_map(|t| t["type"].as_str()).collect();
        assert!(types.contains(&"web_search_20260209"));
        assert!(types.contains(&"web_fetch_20260209"));
        // Declaring code_execution next to them creates a second execution
        // environment — it must stay out.
        assert!(!types.iter().any(|t| t.starts_with("code_execution")));
    }

    #[test]
    fn who_reply_yields_nick_and_operator_flag() {
        let l = parse(":srv 352 me #general u host srv alice H@ :0 Alice").unwrap();
        let m = Member::from_who(&l);
        assert_eq!(m.nick, "alice");
        assert!(m.operator);

        let l = parse(":srv 352 me #general u host srv bob H :0 Bob").unwrap();
        assert!(!Member::from_who(&l).operator);
    }
}
