//! Socket commands — torkd Unix Socket query

use crate::state::AppState;
use crate::torkd_bridge;
use tauri::Manager;

/// Whitelist of allowed torkd commands (prevents command injection)
const ALLOWED_COMMANDS: &[&str] = &[
    "soul", "status", "compile", "run", "halt", "mentor", "dispatch", "ping", "info", "version",
];

#[tauri::command]
pub fn torkd_query(app: tauri::AppHandle, command: String) -> Result<String, String> {
    // Sanitize: reject commands with newlines (injection via torkd protocol)
    if command.contains('\n') || command.contains('\r') {
        return Err("Invalid command: contains newline".to_string());
    }

    // Validate against whitelist
    let base_cmd = command.split_whitespace().next().unwrap_or("");
    if !ALLOWED_COMMANDS.contains(&base_cmd) {
        return Err(format!("Command not allowed: {}", base_cmd));
    }

    // Check torkd connectivity via pid_cache
    let state = app.state::<AppState>();
    let pid = state.pid_cache.lock().unwrap_or_else(|e| e.into_inner());
    if pid.is_none() {
        return Err("torkd not connected".to_string());
    }

    torkd_bridge::query(&command).map_err(|e| format!("torkd query failed: {}", e))
}
