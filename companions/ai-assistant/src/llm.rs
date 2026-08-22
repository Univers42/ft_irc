//! The model backend and the agentic loop.
//!
//! Two wire formats are supported behind one loop, because the loop itself is
//! identical either way: request → the model asks for a tool → run it → hand
//! the result back → repeat until it stops asking.
//!
//! * [`Backend::OpenAi`] — the OpenAI-compatible `/chat/completions` shape.
//!   **Ollama**, llama.cpp's server, LM Studio, vLLM, Groq and OpenRouter all
//!   speak it, so one implementation covers every local and free-tier option.
//!   This is the default: it needs no API key and no account.
//! * [`Backend::Anthropic`] — the Claude Messages API. Better at multi-step
//!   tool use and the only backend with server-side web search, but billed.
//!
//! The two formats disagree about nearly everything, and each disagreement is a
//! place to get it silently wrong:
//!
//! | | Anthropic | OpenAI-compatible |
//! | --- | --- | --- |
//! | system prompt | top-level `system` | first message, `role: "system"` |
//! | tool schema | `input_schema` | `function.parameters` |
//! | tool call | `content[]` block, `input` is an object | `tool_calls[]`, `arguments` is a **JSON string** |
//! | tool result | `tool_result` blocks in ONE user message | one `role: "tool"` message EACH |
//! | finish signal | `stop_reason` | `finish_reason` |

use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};

use crate::config::Config;
use crate::tools::ToolBox;

const ANTHROPIC_VERSION: &str = "2023-06-01";
/// Gates the `fallbacks: "default"` scalar form (Anthropic only).
const FALLBACK_BETA: &str = "server-side-fallback-2026-07-01";
const HTTP_ATTEMPTS: usize = 3;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Backend {
    OpenAi,
    Anthropic,
}

impl Backend {
    /// `ollama` and `openai` are the same wire format; they differ only in the
    /// default base URL, which [`Config`] resolves.
    pub fn parse(s: &str) -> Option<Self> {
        match s.trim().to_lowercase().as_str() {
            "ollama" | "openai" | "openai-compatible" | "local" => Some(Self::OpenAi),
            "anthropic" | "claude" => Some(Self::Anthropic),
            _ => None,
        }
    }

    pub fn label(&self) -> &'static str {
        match self {
            Self::OpenAi => "openai-compatible",
            Self::Anthropic => "anthropic",
        }
    }

    /// Server-side web search exists only on Anthropic. Nothing else can offer
    /// it, so the tool catalog must not advertise it.
    pub fn has_server_tools(&self) -> bool {
        matches!(self, Self::Anthropic)
    }
}

#[derive(Default, Clone, Copy)]
pub struct Usage {
    pub input: u64,
    pub output: u64,
    pub cache_read: u64,
    pub cache_write: u64,
}

impl Usage {
    fn add_anthropic(&mut self, v: &Value) {
        let g = |k: &str| v.get(k).and_then(Value::as_u64).unwrap_or(0);
        self.input += g("input_tokens");
        self.output += g("output_tokens");
        self.cache_read += g("cache_read_input_tokens");
        self.cache_write += g("cache_creation_input_tokens");
    }

    fn add_openai(&mut self, v: &Value) {
        let g = |k: &str| v.get(k).and_then(Value::as_u64).unwrap_or(0);
        self.input += g("prompt_tokens");
        self.output += g("completion_tokens");
    }

    pub fn merge(&mut self, other: &Usage) {
        self.input += other.input;
        self.output += other.output;
        self.cache_read += other.cache_read;
        self.cache_write += other.cache_write;
    }
}

pub enum Answer {
    Text(String),
    /// The model (or its safety layer) declined.
    Refusal(String),
}

pub struct TurnResult {
    pub answer: Answer,
    pub usage: Usage,
    pub tools_used: Vec<String>,
    pub thinking: Option<String>,
    pub served_by: String,
}

/// One tool call, normalised away from whichever wire format produced it.
struct ToolCall {
    id: String,
    name: String,
    input: Value,
}

/// One assistant turn, normalised.
struct Parsed {
    /// The assistant message to append, in the backend's own format.
    message: Value,
    calls: Vec<ToolCall>,
    text: Option<String>,
    /// `refusal`, `pause_turn`, or anything else (treated as "done").
    stop: String,
    thinking: Option<String>,
    served_by: Option<String>,
}

pub struct Agent {
    http: reqwest::Client,
    cfg: Arc<Config>,
}

impl Agent {
    pub fn new(cfg: Arc<Config>) -> Result<Self, String> {
        let http = reqwest::Client::builder()
            .timeout(cfg.api_timeout)
            .build()
            .map_err(|e| format!("building the HTTP client: {e}"))?;
        Ok(Self { http, cfg })
    }

    pub async fn run(
        &self,
        model: &str,
        effort: &str,
        system: &[Value],
        tools: &[Value],
        messages: &mut Vec<Value>,
        toolbox: &ToolBox,
    ) -> Result<TurnResult, String> {
        let backend = self.cfg.backend;
        let mut usage = Usage::default();
        let mut tools_used: Vec<String> = Vec::new();
        let mut thinking: Option<String> = None;
        let mut served_by = model.to_string();
        let mut continuations = 0usize;

        for _ in 0..self.cfg.max_iterations {
            let body = match backend {
                Backend::Anthropic => self.anthropic_body(model, effort, system, tools, messages),
                Backend::OpenAi => self.openai_body(model, system, tools, messages),
            };

            let resp = self.post(&body).await?;

            let parsed = match backend {
                Backend::Anthropic => parse_anthropic(&resp, &mut usage),
                Backend::OpenAi => parse_openai(&resp, &mut usage)?,
            };

            if thinking.is_none() {
                thinking = parsed.thinking.clone();
            }
            if let Some(m) = &parsed.served_by {
                served_by = m.clone();
            }

            // A refusal's content is not an answer — check before reading it.
            if parsed.stop == "refusal" {
                return Ok(TurnResult {
                    answer: Answer::Refusal(parsed.text.unwrap_or_else(|| "declined".into())),
                    usage,
                    tools_used,
                    thinking,
                    served_by,
                });
            }

            messages.push(parsed.message);

            // Anthropic only: the server-side tool loop paused. Resend as-is —
            // adding a "continue" message breaks the resume.
            if parsed.stop == "pause_turn" {
                continuations += 1;
                if continuations <= self.cfg.max_continuations {
                    continue;
                }
                return Ok(TurnResult {
                    answer: Answer::Text(
                        parsed.text.unwrap_or_else(|| "(the search loop kept pausing)".into()),
                    ),
                    usage,
                    tools_used,
                    thinking,
                    served_by,
                });
            }

            if parsed.calls.is_empty() {
                return Ok(TurnResult {
                    answer: Answer::Text(
                        parsed.text.unwrap_or_else(|| "(the model returned no text)".into()),
                    ),
                    usage,
                    tools_used,
                    thinking,
                    served_by,
                });
            }

            // Run every call this turn asked for, then hand the results back in
            // whichever shape the backend expects.
            let mut results = Vec::with_capacity(parsed.calls.len());
            for call in &parsed.calls {
                tools_used.push(call.name.clone());
                let (output, is_error) = toolbox.execute(&call.name, &call.input).await;
                results.push((call.id.clone(), output, is_error));
            }

            match backend {
                Backend::Anthropic => {
                    // All results in ONE user message. Splitting them across
                    // messages trains the model out of parallel tool calls.
                    let blocks: Vec<Value> = results
                        .into_iter()
                        .map(|(id, out, err)| {
                            let mut b = json!({
                                "type": "tool_result",
                                "tool_use_id": id,
                                "content": out,
                            });
                            if err {
                                b["is_error"] = json!(true);
                            }
                            b
                        })
                        .collect();
                    messages.push(json!({"role": "user", "content": blocks}));
                }
                Backend::OpenAi => {
                    // One message per call, each keyed by tool_call_id.
                    for (id, out, _err) in results {
                        messages.push(json!({
                            "role": "tool",
                            "tool_call_id": id,
                            "content": out,
                        }));
                    }
                }
            }
        }

        Err(format!("gave up after {} tool iterations", self.cfg.max_iterations))
    }

    // ── Request building ────────────────────────────────────────────────────

    fn anthropic_body(
        &self,
        model: &str,
        effort: &str,
        system: &[Value],
        tools: &[Value],
        messages: &[Value],
    ) -> Value {
        let mut body = json!({
            "model": model,
            "max_tokens": self.cfg.max_tokens,
            "system": system,
            "messages": messages,
            "thinking": if self.cfg.show_thinking {
                json!({"type": "adaptive", "display": "summarized"})
            } else {
                json!({"type": "adaptive"})
            },
            "output_config": {"effort": effort},
        });
        if !tools.is_empty() {
            body["tools"] = json!(tools);
        }
        if self.cfg.fallbacks {
            body["fallbacks"] = json!("default");
        }
        body
    }

    fn openai_body(
        &self,
        model: &str,
        system: &[Value],
        tools: &[Value],
        messages: &[Value],
    ) -> Value {
        // The system prompt is a message here, not a separate field.
        let mut msgs = vec![json!({"role": "system", "content": flatten_system(system)})];
        msgs.extend(messages.iter().cloned());

        let mut body = json!({
            "model": model,
            "max_tokens": self.cfg.max_tokens,
            "messages": msgs,
            // Small local models drift badly at default temperature and start
            // inventing tool names; a low one keeps calls well-formed.
            "temperature": self.cfg.temperature,
            "stream": false,
        });

        let fns: Vec<Value> = tools.iter().filter_map(to_openai_tool).collect();
        if !fns.is_empty() {
            body["tools"] = json!(fns);
        }
        body
    }

    // ── Transport ───────────────────────────────────────────────────────────

    async fn post(&self, body: &Value) -> Result<Value, String> {
        match self.post_once(body, self.cfg.fallbacks).await {
            // `fallbacks` is Anthropic-only and newer than some models. Retry
            // once without it rather than making people edit a flag.
            Err(PostError::Rejected(msg))
                if self.cfg.fallbacks
                    && self.cfg.backend == Backend::Anthropic
                    && msg.contains("fallback") =>
            {
                eprintln!("llm: server rejected `fallbacks` ({msg}); retrying without it");
                let mut stripped = body.clone();
                if let Some(o) = stripped.as_object_mut() {
                    o.remove("fallbacks");
                }
                self.post_once(&stripped, false).await.map_err(|e| e.to_string())
            }
            other => other.map_err(|e| e.to_string()),
        }
    }

    async fn post_once(&self, body: &Value, fallbacks: bool) -> Result<Value, PostError> {
        let mut last = PostError::Transport("no attempt was made".into());

        for attempt in 1..=HTTP_ATTEMPTS {
            let mut req = self
                .http
                .post(&self.cfg.base_url)
                .header("content-type", "application/json");

            req = match self.cfg.backend {
                Backend::Anthropic => {
                    let mut r = req
                        .header("x-api-key", &self.cfg.api_key)
                        .header("anthropic-version", ANTHROPIC_VERSION);
                    if fallbacks {
                        r = r.header("anthropic-beta", FALLBACK_BETA);
                    }
                    r
                }
                // Ollama ignores the header; hosted OpenAI-compatible providers
                // require it. Sending an empty one would be rejected, so only
                // set it when there is a key.
                Backend::OpenAi => {
                    if self.cfg.api_key.is_empty() {
                        req
                    } else {
                        req.header("authorization", format!("Bearer {}", self.cfg.api_key))
                    }
                }
            };

            let resp = match req.json(body).send().await {
                Ok(r) => r,
                Err(e) => {
                    last = PostError::Transport(e.to_string());
                    backoff(attempt, None).await;
                    continue;
                }
            };

            let status = resp.status();
            let retry_after = resp
                .headers()
                .get("retry-after")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.parse::<u64>().ok());

            let value: Value = match resp.json().await {
                Ok(v) => v,
                Err(e) => {
                    last = PostError::Transport(format!("decoding the response body: {e}"));
                    backoff(attempt, retry_after).await;
                    continue;
                }
            };

            if status.is_success() {
                return Ok(value);
            }

            let msg = value
                .get("error")
                .and_then(|e| e.get("message").or(Some(e)))
                .and_then(|m| m.as_str().map(String::from))
                .unwrap_or_else(|| value.to_string());

            if status.as_u16() == 429 || status.is_server_error() {
                last = PostError::Transport(format!("HTTP {status}: {msg}"));
                backoff(attempt, retry_after).await;
                continue;
            }
            return Err(PostError::Rejected(format!("HTTP {status}: {msg}")));
        }
        Err(last)
    }
}

// ── Response parsing ────────────────────────────────────────────────────────

fn parse_anthropic(resp: &Value, usage: &mut Usage) -> Parsed {
    if let Some(u) = resp.get("usage") {
        usage.add_anthropic(u);
    }
    let stop = resp.get("stop_reason").and_then(Value::as_str).unwrap_or("").to_string();

    if stop == "refusal" {
        let why = resp
            .get("stop_details")
            .and_then(|d| d.get("category"))
            .and_then(Value::as_str)
            .unwrap_or("unspecified");
        return Parsed {
            message: json!({"role": "assistant", "content": []}),
            calls: Vec::new(),
            text: Some(why.to_string()),
            stop,
            thinking: None,
            served_by: None,
        };
    }

    let content = resp.get("content").and_then(Value::as_array).cloned().unwrap_or_default();

    let calls = content
        .iter()
        .filter(|b| b.get("type").and_then(Value::as_str) == Some("tool_use"))
        .map(|b| ToolCall {
            id: b.get("id").and_then(Value::as_str).unwrap_or("").to_string(),
            name: b.get("name").and_then(Value::as_str).unwrap_or("").to_string(),
            input: b.get("input").cloned().unwrap_or_else(|| json!({})),
        })
        .collect();

    let text = join_blocks(&content, "text", "text");
    let thinking = join_blocks(&content, "thinking", "thinking");

    Parsed {
        message: json!({"role": "assistant", "content": content}),
        calls,
        text,
        stop,
        thinking,
        served_by: resp.get("model").and_then(Value::as_str).map(String::from),
    }
}

fn parse_openai(resp: &Value, usage: &mut Usage) -> Result<Parsed, String> {
    if let Some(u) = resp.get("usage") {
        usage.add_openai(u);
    }

    let choice = resp
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|c| c.first())
        .ok_or_else(|| format!("no choices in the response: {resp}"))?;

    let message = choice.get("message").cloned().unwrap_or_else(|| json!({"role": "assistant"}));
    let stop = choice
        .get("finish_reason")
        .and_then(Value::as_str)
        .unwrap_or("stop")
        .to_string();

    let mut calls = Vec::new();
    if let Some(list) = message.get("tool_calls").and_then(Value::as_array) {
        for c in list {
            let f = c.get("function").unwrap_or(&Value::Null);
            let name = f.get("name").and_then(Value::as_str).unwrap_or("").to_string();
            // `arguments` is a JSON *string*, and small models sometimes emit
            // invalid JSON in it. A parse failure must not kill the turn — pass
            // an empty object and let the tool report the missing parameter.
            let raw = f.get("arguments").and_then(Value::as_str).unwrap_or("{}");
            let input = serde_json::from_str(raw).unwrap_or_else(|_| {
                eprintln!("llm: tool `{name}` had unparseable arguments: {raw}");
                json!({})
            });
            calls.push(ToolCall {
                id: c.get("id").and_then(Value::as_str).unwrap_or("").to_string(),
                name,
                input,
            });
        }
    }

    let text = message
        .get("content")
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(String::from);

    // Some local models emit a null content alongside tool_calls; the field has
    // to survive the round trip or the next request is malformed.
    let mut message = message;
    if message.get("content").is_none() {
        message["content"] = Value::Null;
    }

    Ok(Parsed {
        message,
        calls,
        text,
        stop,
        thinking: None,
        served_by: resp.get("model").and_then(Value::as_str).map(String::from),
    })
}

/// Translate one Anthropic-shaped tool definition into an OpenAI function.
/// Server-side tools (`web_search_20260209`) have no schema and cannot be
/// translated — they are dropped, which is why the catalog gates them on the
/// backend in the first place.
fn to_openai_tool(t: &Value) -> Option<Value> {
    let name = t.get("name")?.as_str()?;
    let schema = t.get("input_schema")?;
    Some(json!({
        "type": "function",
        "function": {
            "name": name,
            "description": t.get("description").and_then(Value::as_str).unwrap_or(""),
            "parameters": schema,
        }
    }))
}

fn flatten_system(system: &[Value]) -> String {
    system
        .iter()
        .filter_map(|b| b.get("text").and_then(Value::as_str))
        .collect::<Vec<&str>>()
        .join("\n\n")
}

fn join_blocks(content: &[Value], kind: &str, field: &str) -> Option<String> {
    let text = content
        .iter()
        .filter(|b| b.get("type").and_then(Value::as_str) == Some(kind))
        .filter_map(|b| b.get(field).and_then(Value::as_str))
        .collect::<Vec<&str>>()
        .join(" ");
    let text = text.trim();
    (!text.is_empty()).then(|| text.to_string())
}

enum PostError {
    Transport(String),
    Rejected(String),
}

impl std::fmt::Display for PostError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PostError::Transport(m) | PostError::Rejected(m) => write!(f, "{m}"),
        }
    }
}

async fn backoff(attempt: usize, retry_after: Option<u64>) {
    let secs = retry_after.unwrap_or(1 << (attempt - 1)).min(30);
    tokio::time::sleep(Duration::from_secs(secs)).await;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn backend_names_map_to_wire_formats() {
        assert!(matches!(Backend::parse("ollama"), Some(Backend::OpenAi)));
        assert!(matches!(Backend::parse("Claude"), Some(Backend::Anthropic)));
        assert!(Backend::parse("gpt5").is_none());
        assert!(!Backend::OpenAi.has_server_tools());
        assert!(Backend::Anthropic.has_server_tools());
    }

    #[test]
    fn tool_schema_is_translated_to_a_function() {
        let anthropic = json!({
            "name": "irc_kick",
            "description": "remove someone",
            "input_schema": {"type": "object", "properties": {"nick": {"type": "string"}}}
        });
        let f = to_openai_tool(&anthropic).unwrap();
        assert_eq!(f["type"], "function");
        assert_eq!(f["function"]["name"], "irc_kick");
        assert_eq!(f["function"]["parameters"]["properties"]["nick"]["type"], "string");
    }

    #[test]
    fn server_tools_cannot_be_translated_and_are_dropped() {
        let web = json!({"type": "web_search_20260209", "name": "web_search"});
        assert!(to_openai_tool(&web).is_none());
    }

    #[test]
    fn openai_tool_arguments_are_parsed_from_their_json_string() {
        let resp = json!({
            "model": "qwen2.5:1.5b",
            "choices": [{"finish_reason": "tool_calls", "message": {
                "role": "assistant", "content": null,
                "tool_calls": [{"id": "call_1", "type": "function", "function": {
                    "name": "irc_channel_members",
                    "arguments": "{\"channel\": \"#general\"}"
                }}]
            }}],
            "usage": {"prompt_tokens": 100, "completion_tokens": 20}
        });
        let mut u = Usage::default();
        let p = parse_openai(&resp, &mut u).unwrap();
        assert_eq!(p.calls.len(), 1);
        assert_eq!(p.calls[0].name, "irc_channel_members");
        assert_eq!(p.calls[0].input["channel"], "#general");
        assert_eq!((u.input, u.output), (100, 20));
    }

    #[test]
    fn malformed_tool_arguments_do_not_kill_the_turn() {
        // Small models do emit broken JSON here. It must degrade to an empty
        // object so the tool can report the missing parameter.
        let resp = json!({
            "choices": [{"finish_reason": "tool_calls", "message": {
                "role": "assistant",
                "tool_calls": [{"id": "c", "function": {"name": "x", "arguments": "{broken"}}]
            }}]
        });
        let mut u = Usage::default();
        let p = parse_openai(&resp, &mut u).unwrap();
        assert_eq!(p.calls[0].input, json!({}));
    }

    #[test]
    fn openai_plain_text_answer_is_extracted() {
        let resp = json!({
            "choices": [{"finish_reason": "stop",
                "message": {"role": "assistant", "content": "  hello  "}}]
        });
        let mut u = Usage::default();
        let p = parse_openai(&resp, &mut u).unwrap();
        assert_eq!(p.text.unwrap(), "hello");
        assert!(p.calls.is_empty());
    }

    #[test]
    fn anthropic_refusal_is_reported_before_content_is_read() {
        let resp = json!({
            "stop_reason": "refusal",
            "stop_details": {"type": "refusal", "category": "cyber"},
            "content": [{"type": "text", "text": "I can't help with that"}]
        });
        let mut u = Usage::default();
        let p = parse_anthropic(&resp, &mut u);
        assert_eq!(p.stop, "refusal");
        assert_eq!(p.text.unwrap(), "cyber");
    }

    #[test]
    fn system_blocks_flatten_for_the_openai_shape() {
        let sys = vec![json!({"type": "text", "text": "a"}), json!({"type": "text", "text": "b"})];
        assert_eq!(flatten_system(&sys), "a\n\nb");
    }
}
