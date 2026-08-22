//! Configuration — every knob is an environment variable, nothing on disk.
//!
//! `IRC_*` keeps the names the first version used. Model settings are `AI_*`,
//! with the old `ANTHROPIC_*` spellings still honoured as fallbacks so an
//! existing `.env` keeps working.

use std::time::Duration;

use crate::llm::Backend;

/// The effort ladder the Anthropic API accepts. Anything else is a 400, so an
/// unrecognised value is corrected here rather than at the first request.
const EFFORTS: [&str; 5] = ["low", "medium", "high", "xhigh", "max"];

/// Ollama's OpenAI-compatible endpoint, which is the zero-cost default.
const OLLAMA_URL: &str = "http://127.0.0.1:11434/v1/chat/completions";
const ANTHROPIC_URL: &str = "https://api.anthropic.com/v1/messages";

#[derive(Clone)]
pub struct Config {
    // ── IRC ────────────────────────────────────────────────────────────────
    pub host: String,
    pub port: u16,
    pub pass: String,
    pub nick: String,
    pub realname: String,
    pub channels: Vec<String>,

    // ── Model ──────────────────────────────────────────────────────────────
    pub backend: Backend,
    pub base_url: String,
    pub api_key: String,
    pub model: String,
    /// Anthropic only.
    pub effort: String,
    /// OpenAI-compatible only. Low, because small local models drift into
    /// inventing tool names at higher temperatures.
    pub temperature: f32,
    pub max_tokens: u32,
    pub api_timeout: Duration,

    // ── Behaviour ──────────────────────────────────────────────────────────
    pub persona: Option<String>,
    pub history_messages: usize,
    /// Lines of passive channel scrollback kept per target.
    pub log_lines: usize,
    pub max_iterations: usize,
    pub max_continuations: usize,
    pub max_concurrent: usize,
    pub cooldown: Duration,
    pub query_timeout: Duration,
    pub show_tools: bool,
    pub show_thinking: bool,

    // ── Capability gates ───────────────────────────────────────────────────
    pub web_tools: bool,
    pub fallbacks: bool,
    pub allow_channel_ops: bool,
    pub allow_moderation: bool,
    pub admins: Vec<String>,
}

impl Config {
    pub fn from_env() -> Result<Self, String> {
        let raw_backend = env_str("AI_BACKEND", "ollama");
        let backend = Backend::parse(&raw_backend).ok_or_else(|| {
            format!("AI_BACKEND={raw_backend:?} is not one of: ollama, openai, anthropic")
        })?;

        // Only Anthropic hard-requires a key. Ollama needs none at all, and a
        // hosted OpenAI-compatible provider will 401 clearly if one is missing.
        let api_key = first_env(&["AI_API_KEY", "ANTHROPIC_API_KEY"]).unwrap_or_default();
        if backend == Backend::Anthropic && api_key.is_empty() {
            return Err("AI_BACKEND=anthropic needs ANTHROPIC_API_KEY (or AI_API_KEY)".into());
        }

        let base_url = match std::env::var("AI_BASE_URL") {
            Ok(u) if !u.trim().is_empty() => normalise_url(u.trim(), backend),
            _ => match backend {
                Backend::OpenAi => OLLAMA_URL.to_string(),
                Backend::Anthropic => ANTHROPIC_URL.to_string(),
            },
        };

        let model = first_env(&["AI_MODEL", "ANTHROPIC_MODEL"]).unwrap_or_else(|| {
            match backend {
                // ~1 GB, runs on CPU, and one of the smallest models that can
                // still hold a tool-calling format together.
                Backend::OpenAi => "qwen2.5:1.5b".to_string(),
                Backend::Anthropic => "claude-opus-5".to_string(),
            }
        });

        let effort = first_env(&["AI_EFFORT", "ANTHROPIC_EFFORT"])
            .unwrap_or_else(|| "medium".into())
            .to_lowercase();
        let effort = if EFFORTS.contains(&effort.as_str()) {
            effort
        } else {
            eprintln!("config: effort {effort:?} is not one of {EFFORTS:?}; using \"medium\"");
            "medium".to_string()
        };

        // Server-side web search exists only on Anthropic. Asking for it
        // anywhere else would put a tool in the catalog that cannot be called.
        let want_web = env_bool("AI_WEB_TOOLS", true);
        let web_tools = want_web && backend.has_server_tools();
        if want_web && !web_tools {
            eprintln!(
                "config: web tools are Anthropic-only (server-side); disabled for the \
                 {} backend",
                backend.label()
            );
        }

        Ok(Self {
            host: env_str("IRC_HOST", "127.0.0.1"),
            port: env_num("IRC_PORT", 6667),
            pass: env_str("IRC_PASS", ""),
            nick: env_str("IRC_NICK", "assistant"),
            realname: env_str("IRC_REALNAME", "AI assistant"),
            channels: env_list("IRC_CHANNELS"),

            backend,
            base_url,
            api_key,
            model,
            effort,
            temperature: env_num("AI_TEMPERATURE", 0.2),
            max_tokens: first_env(&["AI_MAX_TOKENS", "ANTHROPIC_MAX_TOKENS"])
                .and_then(|s| s.parse().ok())
                // Local models are slow; a smaller ceiling keeps IRC responsive.
                .unwrap_or(if backend == Backend::Anthropic { 8000 } else { 1024 }),
            api_timeout: Duration::from_secs(
                first_env(&["AI_TIMEOUT_SECS", "ANTHROPIC_TIMEOUT_SECS"])
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(180),
            ),

            persona: std::env::var("AI_PERSONA").ok().filter(|s| !s.trim().is_empty()),
            history_messages: env_num("AI_HISTORY_MESSAGES", 24),
            log_lines: env_num("AI_LOG_LINES", 300),
            max_iterations: env_num("AI_MAX_ITERATIONS", 12),
            max_continuations: env_num("AI_MAX_CONTINUATIONS", 5),
            max_concurrent: env_num::<usize>("AI_MAX_CONCURRENT", 3).max(1),
            cooldown: Duration::from_secs(env_num("AI_COOLDOWN_SECS", 5)),
            query_timeout: Duration::from_secs(env_num("AI_QUERY_TIMEOUT_SECS", 5)),
            show_tools: env_bool("AI_SHOW_TOOLS", true),
            show_thinking: env_bool("AI_SHOW_THINKING", false),

            web_tools,
            fallbacks: env_bool("AI_FALLBACKS", true) && backend == Backend::Anthropic,
            allow_channel_ops: env_bool("AI_ALLOW_CHANNEL_OPS", true),
            allow_moderation: env_bool("AI_ALLOW_MODERATION", false),
            admins: env_list("AI_ADMINS"),
        })
    }

    /// Admin check. Nicks are the only identity this server has (no accounts,
    /// no SASL), so this is a weak gate on its own — every channel-scoped
    /// moderation tool additionally verifies live operator status via WHO.
    pub fn is_admin(&self, nick: &str) -> bool {
        self.admins.iter().any(|a| a.eq_ignore_ascii_case(nick))
    }
}

/// Accept a base URL with or without the endpoint path, because every provider
/// documents it differently (`http://host:11434`, `.../v1`, `.../v1/chat/completions`).
fn normalise_url(url: &str, backend: Backend) -> String {
    let trimmed = url.trim_end_matches('/');
    match backend {
        Backend::Anthropic => {
            if trimmed.ends_with("/messages") {
                trimmed.to_string()
            } else {
                format!("{trimmed}/v1/messages")
            }
        }
        Backend::OpenAi => {
            if trimmed.ends_with("/chat/completions") {
                trimmed.to_string()
            } else if trimmed.ends_with("/v1") {
                format!("{trimmed}/chat/completions")
            } else {
                format!("{trimmed}/v1/chat/completions")
            }
        }
    }
}

fn env_str(key: &str, default: &str) -> String {
    std::env::var(key).unwrap_or_else(|_| default.to_string())
}

/// First of `keys` that is set and non-empty.
fn first_env(keys: &[&str]) -> Option<String> {
    keys.iter()
        .filter_map(|k| std::env::var(k).ok())
        .map(|v| v.trim().to_string())
        .find(|v| !v.is_empty())
}

fn env_num<T: std::str::FromStr>(key: &str, default: T) -> T {
    std::env::var(key).ok().and_then(|s| s.trim().parse().ok()).unwrap_or(default)
}

fn env_bool(key: &str, default: bool) -> bool {
    match std::env::var(key) {
        Ok(v) => matches!(v.trim().to_lowercase().as_str(), "1" | "true" | "yes" | "on"),
        Err(_) => default,
    }
}

fn env_list(key: &str) -> Vec<String> {
    std::env::var(key)
        .unwrap_or_default()
        .split(',')
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(String::from)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn base_urls_are_accepted_in_every_documented_spelling() {
        for input in [
            "http://localhost:11434",
            "http://localhost:11434/",
            "http://localhost:11434/v1",
            "http://localhost:11434/v1/chat/completions",
        ] {
            assert_eq!(
                normalise_url(input, Backend::OpenAi),
                "http://localhost:11434/v1/chat/completions",
                "for {input}"
            );
        }
        assert_eq!(
            normalise_url("https://api.anthropic.com", Backend::Anthropic),
            "https://api.anthropic.com/v1/messages"
        );
    }
}
