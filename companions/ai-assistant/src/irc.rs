//! IRC plumbing: line parsing, a single-writer outbound path, and a registry
//! that lets a spawned task `await` a numeric reply.
//!
//! The registry is what makes tools like `irc_channel_members` possible. A tool
//! runs inside a task spawned off the read loop, but the WHO reply arrives on
//! that same read loop — so the tool registers a [`QuerySpec`], sends `WHO`, and
//! waits on a oneshot the read loop completes when the terminating numeric
//! (`315`) shows up.

use std::collections::HashSet;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use tokio::sync::{mpsc, oneshot, Mutex};

/// RFC 2812: 512 bytes on the wire, CRLF included.
pub const MAX_LINE: usize = 512;

/// ASCII casemapping — what this server advertises (`CASEMAPPING=ascii`).
/// Deliberately *not* Unicode-aware: folding accented letters would make two
/// distinct nicks compare equal, which is exactly the impersonation vector the
/// server avoids.
pub fn irc_lower(s: &str) -> String {
    s.to_ascii_lowercase()
}

pub fn irc_eq(a: &str, b: &str) -> bool {
    a.eq_ignore_ascii_case(b)
}

// ─────────────────────────────────────────────────────────────────────────────
//  Parsing
// ─────────────────────────────────────────────────────────────────────────────

#[derive(Clone, Debug)]
pub struct Line {
    /// Source prefix without the leading ':' — empty when the line had none.
    pub prefix: String,
    pub command: String,
    pub params: Vec<String>,
}

impl Line {
    /// The nick half of the prefix (`nick!user@host` → `nick`).
    pub fn nick(&self) -> &str {
        let end = self
            .prefix
            .find(['!', '@'])
            .unwrap_or(self.prefix.len());
        &self.prefix[..end]
    }

    pub fn param(&self, i: usize) -> &str {
        self.params.get(i).map(String::as_str).unwrap_or("")
    }
}

/// Parse `[:prefix] COMMAND [params...] [:trailing]`.
pub fn parse(raw: &str) -> Option<Line> {
    let mut rest = raw.trim_end_matches(['\r', '\n']);
    if rest.is_empty() {
        return None;
    }

    let mut prefix = String::new();
    if let Some(after) = rest.strip_prefix(':') {
        let (p, r) = after.split_once(' ')?;
        prefix = p.to_string();
        rest = r.trim_start();
    }

    let mut params = Vec::new();
    let command;
    {
        let (cmd, r) = match rest.split_once(' ') {
            Some((c, r)) => (c, r.trim_start()),
            None => (rest, ""),
        };
        command = cmd.to_ascii_uppercase();
        rest = r;
    }

    while !rest.is_empty() {
        if let Some(trailing) = rest.strip_prefix(':') {
            params.push(trailing.to_string());
            break;
        }
        match rest.split_once(' ') {
            Some((p, r)) => {
                params.push(p.to_string());
                rest = r.trim_start();
            }
            None => {
                params.push(rest.to_string());
                break;
            }
        }
    }

    Some(Line { prefix, command, params })
}

/// Strip everything that could break out of one IRC line. `\x01` (CTCP) is left
/// alone — the server passes it through and clients render `ACTION` from it.
pub fn sanitize(text: &str) -> String {
    text.chars()
        .map(|c| match c {
            '\r' | '\n' | '\0' => ' ',
            _ => c,
        })
        .collect::<String>()
        .trim()
        .to_string()
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pending numeric queries
// ─────────────────────────────────────────────────────────────────────────────

/// One line shape a query is interested in.
///
/// `at` is where the subject (channel or nick) sits in `params`. Numerics put
/// the recipient in `params[0]` and the subject in `params[1]`; a command echo
/// (`:me KICK #chan bob`) puts it in `params[0]`. The error numerics are less
/// regular — `482` has the channel at 1 but `441` has it at 2 — so those use
/// `None`, "any parameter equals the subject".
pub struct Rule {
    pub command: &'static str,
    pub at: Option<usize>,
    /// This line ends the burst.
    pub terminal: bool,
}

impl Rule {
    pub const fn collect(command: &'static str, at: usize) -> Self {
        Self { command, at: Some(at), terminal: false }
    }
    pub const fn finish(command: &'static str, at: usize) -> Self {
        Self { command, at: Some(at), terminal: true }
    }
    /// Terminal, matched on any parameter — for the error numerics.
    pub const fn err(command: &'static str) -> Self {
        Self { command, at: None, terminal: true }
    }
}

pub struct QuerySpec {
    pub rules: &'static [Rule],
    /// The channel or nick every rule is matched against.
    pub arg: String,
}

impl QuerySpec {
    fn matches(&self, line: &Line) -> Option<bool> {
        for rule in self.rules {
            if !irc_eq(rule.command, &line.command) {
                continue;
            }
            let hit = match rule.at {
                Some(i) => irc_eq(line.param(i), &self.arg),
                None => line.params.iter().any(|p| irc_eq(p, &self.arg)),
            };
            if hit {
                return Some(rule.terminal);
            }
        }
        None
    }
}

struct Pending {
    id: u64,
    spec: QuerySpec,
    lines: Vec<Line>,
    tx: Option<oneshot::Sender<Vec<Line>>>,
}

#[derive(Default)]
pub struct State {
    /// The nick the server accepted (which may be truncated to NICKLEN).
    pub nick: String,
    /// Length of `:nick!user@host ` as the server writes it when relaying our
    /// messages. Learned from the server's echo of our own JOIN; until then a
    /// conservative estimate keeps replies inside 512 bytes anyway.
    pub prefix_len: usize,
    pub nicklen: usize,
    pub registered: bool,
    /// Joined channels, casemapped.
    pub channels: HashSet<String>,
}

/// A cheap-to-clone handle to one IRC session.
#[derive(Clone)]
pub struct Irc {
    out: mpsc::Sender<String>,
    pending: Arc<Mutex<Vec<Pending>>>,
    state: Arc<Mutex<State>>,
    next_id: Arc<AtomicU64>,
    query_timeout: Duration,
}

impl Irc {
    pub fn new(out: mpsc::Sender<String>, query_timeout: Duration) -> Self {
        Self {
            out,
            pending: Arc::new(Mutex::new(Vec::new())),
            state: Arc::new(Mutex::new(State {
                // Worst case under this server's limits: 9 (NICKLEN) + 10 user
                // + 63 host + the ':', '!', '@' and trailing space.
                prefix_len: 86,
                nicklen: 9,
                ..State::default()
            })),
            next_id: Arc::new(AtomicU64::new(1)),
            query_timeout,
        }
    }

    pub async fn state(&self) -> State {
        let s = self.state.lock().await;
        State {
            nick: s.nick.clone(),
            prefix_len: s.prefix_len,
            nicklen: s.nicklen,
            registered: s.registered,
            channels: s.channels.clone(),
        }
    }

    pub async fn with_state<R>(&self, f: impl FnOnce(&mut State) -> R) -> R {
        let mut s = self.state.lock().await;
        f(&mut s)
    }

    pub async fn nick(&self) -> String {
        self.state.lock().await.nick.clone()
    }

    pub async fn in_channel(&self, chan: &str) -> bool {
        self.state.lock().await.channels.contains(&irc_lower(chan))
    }

    /// Queue one raw line. Returns false once the writer task is gone, which is
    /// how a caller learns the session died.
    pub async fn send_raw(&self, line: impl Into<String>) -> bool {
        self.out.send(line.into()).await.is_ok()
    }

    /// Split `text` so that every relayed line stays inside 512 bytes, then
    /// queue each piece as a PRIVMSG.
    pub async fn privmsg(&self, target: &str, text: &str) {
        for chunk in self.chunk(target, text, "PRIVMSG").await {
            if !self.send_raw(chunk).await {
                return;
            }
        }
    }

    pub async fn notice(&self, target: &str, text: &str) {
        for chunk in self.chunk(target, text, "NOTICE").await {
            if !self.send_raw(chunk).await {
                return;
            }
        }
    }

    /// Budget the payload against the *relayed* form, not the form we send.
    /// The server re-frames our line with `:nick!user@host ` before handing it
    /// to members and caps the result at 512 — so a line that is legal on the
    /// way in can still be truncated on the way out. Budget for the prefix here
    /// and nothing gets clipped.
    async fn chunk(&self, target: &str, text: &str, verb: &str) -> Vec<String> {
        let prefix_len = self.state.lock().await.prefix_len;
        // MAX_LINE includes the CRLF; then the envelope the server prepends.
        let budget = (MAX_LINE - 2)
            .saturating_sub(prefix_len + verb.len() + 1 + target.len() + 2)
            .max(80);

        let mut out = Vec::new();
        for raw_line in sanitize(text).split('\n') {
            let line = raw_line.trim();
            if line.is_empty() {
                continue;
            }
            for piece in wrap(line, budget) {
                out.push(format!("{verb} {target} :{piece}"));
            }
        }
        if out.is_empty() {
            out.push(format!("{verb} {target} :(empty reply)"));
        }
        out
    }

    /// Send `request` and collect the numerics `spec` describes.
    ///
    /// Always returns: on timeout it yields whatever arrived, because several of
    /// these bursts (`324` RPL_CHANNELMODEIS, `332` RPL_TOPIC) have no
    /// terminating numeric at all.
    pub async fn query(&self, request: String, spec: QuerySpec) -> Vec<Line> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let (tx, rx) = oneshot::channel();
        self.pending.lock().await.push(Pending {
            id,
            spec,
            lines: Vec::new(),
            tx: Some(tx),
        });

        if !self.send_raw(request).await {
            self.take_pending(id).await;
            return Vec::new();
        }

        match tokio::time::timeout(self.query_timeout, rx).await {
            Ok(Ok(lines)) => lines,
            // Timed out, or the read loop dropped the sender: reclaim whatever
            // the entry had gathered before giving up on it.
            _ => self.take_pending(id).await,
        }
    }

    async fn take_pending(&self, id: u64) -> Vec<Line> {
        let mut pend = self.pending.lock().await;
        match pend.iter().position(|p| p.id == id) {
            Some(i) => pend.remove(i).lines,
            None => Vec::new(),
        }
    }

    /// Offer one inbound line to the waiting queries. Called from the read loop
    /// for every line, before any other dispatch.
    pub async fn feed(&self, line: &Line) {
        let mut pend = self.pending.lock().await;
        let Some((idx, terminal)) = pend
            .iter()
            .enumerate()
            .find_map(|(i, p)| p.spec.matches(line).map(|t| (i, t)))
        else {
            return;
        };

        pend[idx].lines.push(line.clone());
        if terminal {
            let mut done = pend.remove(idx);
            if let Some(tx) = done.tx.take() {
                let _ = tx.send(done.lines);
            }
        }
    }
}

/// Greedy word wrap, with a hard split for a single word longer than `budget`.
/// Widths are byte counts (the IRC limit is bytes), but splits land on char
/// boundaries so UTF-8 survives.
fn wrap(line: &str, budget: usize) -> Vec<String> {
    let mut out = Vec::new();
    let mut buf = String::new();

    for word in line.split(' ').filter(|w| !w.is_empty()) {
        if !buf.is_empty() && buf.len() + 1 + word.len() > budget {
            out.push(std::mem::take(&mut buf));
        }
        if word.len() > budget {
            if !buf.is_empty() {
                out.push(std::mem::take(&mut buf));
            }
            let mut rest = word;
            while rest.len() > budget {
                let mut cut = budget;
                while cut > 0 && !rest.is_char_boundary(cut) {
                    cut -= 1;
                }
                if cut == 0 {
                    break;
                }
                out.push(rest[..cut].to_string());
                rest = &rest[cut..];
            }
            buf.push_str(rest);
            continue;
        }
        if !buf.is_empty() {
            buf.push(' ');
        }
        buf.push_str(word);
    }
    if !buf.is_empty() {
        out.push(buf);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_a_privmsg_with_trailing() {
        let l = parse(":bob!u@h PRIVMSG #general :hello there").unwrap();
        assert_eq!(l.nick(), "bob");
        assert_eq!(l.command, "PRIVMSG");
        assert_eq!(l.param(0), "#general");
        assert_eq!(l.param(1), "hello there");
    }

    #[test]
    fn parses_a_numeric_with_no_trailing() {
        let l = parse(":srv 324 me #chan +itk key").unwrap();
        assert_eq!(l.command, "324");
        assert_eq!(l.param(1), "#chan");
        assert_eq!(l.param(3), "key");
    }

    #[test]
    fn trailing_may_contain_colons_and_spaces() {
        let l = parse(":srv 332 me #chan :topic: a :b: c").unwrap();
        assert_eq!(l.param(2), "topic: a :b: c");
    }

    #[test]
    fn sanitize_kills_line_injection() {
        assert_eq!(sanitize("a\r\nQUIT :x"), "a  QUIT :x");
    }

    #[test]
    fn wrap_splits_an_overlong_word() {
        let parts = wrap(&"x".repeat(25), 10);
        assert_eq!(parts.len(), 3);
        assert!(parts.iter().all(|p| p.len() <= 10));
    }

    #[test]
    fn wrap_prefers_word_boundaries() {
        assert_eq!(wrap("aaa bbb ccc", 7), vec!["aaa bbb", "ccc"]);
    }
}
