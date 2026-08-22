//! Two kinds of state, kept apart on purpose.
//!
//! * **Scrollback** — every channel line the bot sees, addressed to it or not.
//!   It is *not* fed into the prompt; it is what the `irc_recent_messages` tool
//!   reads. Keeping it out of the prompt is what lets the model summarise 200
//!   lines on request without paying for them on every unrelated question.
//! * **Conversations** — the Messages API `messages` array per reply
//!   destination, carrying full content blocks so `tool_use`/`tool_result`
//!   pairs and thinking blocks survive across turns.

use std::collections::HashMap;
use std::sync::Arc;

use serde_json::Value;
use tokio::sync::Mutex;

use crate::irc::irc_lower;

#[derive(Clone)]
pub struct Entry {
    pub at: std::time::SystemTime,
    pub nick: String,
    pub text: String,
}

#[derive(Default)]
struct Inner {
    logs: HashMap<String, Vec<Entry>>,
    convos: HashMap<String, Vec<Value>>,
}

#[derive(Clone)]
pub struct Memory {
    inner: Arc<Mutex<Inner>>,
    log_lines: usize,
    history_messages: usize,
}

impl Memory {
    pub fn new(log_lines: usize, history_messages: usize) -> Self {
        Self {
            inner: Arc::new(Mutex::new(Inner::default())),
            log_lines,
            history_messages,
        }
    }

    pub async fn record(&self, target: &str, nick: &str, text: &str) {
        let mut inner = self.inner.lock().await;
        let log = inner.logs.entry(irc_lower(target)).or_default();
        log.push(Entry {
            at: std::time::SystemTime::now(),
            nick: nick.to_string(),
            text: text.to_string(),
        });
        if log.len() > self.log_lines {
            let cut = log.len() - self.log_lines;
            log.drain(0..cut);
        }
    }

    /// The most recent `limit` lines for `target`, oldest first.
    pub async fn recent(&self, target: &str, limit: usize) -> Vec<Entry> {
        let inner = self.inner.lock().await;
        match inner.logs.get(&irc_lower(target)) {
            Some(log) => log[log.len().saturating_sub(limit)..].to_vec(),
            None => Vec::new(),
        }
    }

    pub async fn conversation(&self, dest: &str) -> Vec<Value> {
        self.inner
            .lock()
            .await
            .convos
            .get(&irc_lower(dest))
            .cloned()
            .unwrap_or_default()
    }

    /// Replace a conversation with the messages the completed turn produced,
    /// trimmed to the configured window.
    pub async fn store_conversation(&self, dest: &str, mut messages: Vec<Value>) {
        trim(&mut messages, self.history_messages);
        self.inner.lock().await.convos.insert(irc_lower(dest), messages);
    }

    pub async fn forget(&self, dest: &str) -> bool {
        self.inner.lock().await.convos.remove(&irc_lower(dest)).is_some()
    }
}

/// Trim from the front, then keep dropping until the window opens on a plain
/// `user` message.
///
/// Cutting anywhere else corrupts the request: a leading `assistant` message is
/// rejected outright, and a `user` message whose content is `tool_result`
/// blocks references a `tool_use` id that no longer exists in the history.
fn trim(messages: &mut Vec<Value>, max: usize) {
    if messages.len() > max {
        messages.drain(0..messages.len() - max);
    }
    while messages.first().is_some_and(|m| !is_plain_user(m)) {
        messages.remove(0);
    }
}

fn is_plain_user(m: &Value) -> bool {
    if m.get("role").and_then(Value::as_str) != Some("user") {
        return false;
    }
    match m.get("content") {
        Some(Value::Array(blocks)) => !blocks
            .iter()
            .any(|b| b.get("type").and_then(Value::as_str) == Some("tool_result")),
        _ => true,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn user(text: &str) -> Value {
        json!({"role": "user", "content": text})
    }
    fn assistant(text: &str) -> Value {
        json!({"role": "assistant", "content": [{"type": "text", "text": text}]})
    }
    fn tool_result() -> Value {
        json!({"role": "user", "content": [{"type": "tool_result", "tool_use_id": "t1", "content": "ok"}]})
    }

    #[test]
    fn trim_opens_the_window_on_a_plain_user_message() {
        let mut m = vec![user("q1"), assistant("a1"), user("q2"), assistant("a2")];
        trim(&mut m, 2);
        assert_eq!(m.len(), 2);
        assert_eq!(m[0]["role"], "user");
        assert_eq!(m[0]["content"], "q2");
    }

    #[test]
    fn trim_never_leaves_an_orphan_tool_result() {
        // Cutting to 3 would start the window at the tool_result, whose
        // tool_use_id no longer exists — those must be dropped too.
        let mut m = vec![user("q1"), assistant("call"), tool_result(), assistant("a1"), user("q2")];
        trim(&mut m, 3);
        assert_eq!(m.len(), 1);
        assert_eq!(m[0]["content"], "q2");
    }

    #[tokio::test]
    async fn scrollback_is_bounded_and_ordered_oldest_first() {
        let mem = Memory::new(3, 10);
        for i in 0..5 {
            mem.record("#general", "bob", &format!("m{i}")).await;
        }
        let recent = mem.recent("#GENERAL", 10).await;
        assert_eq!(recent.len(), 3);
        assert_eq!(recent[0].text, "m2");
        assert_eq!(recent[2].text, "m4");
    }
}
