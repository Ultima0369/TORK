//! Socket commands — torkd Unix Socket query

use crate::state::AppState;
use crate::torkd_bridge;
use tauri::Manager;

/// Whitelist of allowed torkd commands (prevents command injection)
/// Prefix-based: "exec:" allows "exec:ls", "task:" allows "task:exec:cmd", etc.
const ALLOWED_PREFIXES: &[&str] = &[
    "ping", "status", "soul", "state", "mentor",
    "exec:", "audit:", "codegen:", "task:", "result:",
    "tasks", "dispatch", "exit", "quit",
];

#[tauri::command]
pub fn torkd_query(app: tauri::AppHandle, command: String) -> Result<String, String> {
    // Sanitize: reject commands with newlines (injection via torkd protocol)
    if command.contains('\n') || command.contains('\r') {
        return Err("Invalid command: contains newline".to_string());
    }

    // Validate against prefix whitelist
    let allowed = ALLOWED_PREFIXES.iter().any(|prefix| command.starts_with(prefix));
    if !allowed {
        return Err(format!("Command not allowed: {}", command));
    }

    // Check torkd connectivity via pid_cache
    let state = app.state::<AppState>();
    let pid = state.pid_cache.lock().unwrap_or_else(|e| e.into_inner());
    if pid.is_none() {
        return Err("torkd not connected".to_string());
    }

    torkd_bridge::query(&command).map_err(|e| format!("torkd query failed: {}", e))
}
