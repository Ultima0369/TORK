//! Process watcher — monitors PID liveness every 500ms
//! Emits "engine-started" / "engine-stopped" events on state changes
//! Updates AppState pid_cache for poll_loop to use

use crate::soul_parser;
use tauri::{AppHandle, Emitter, Manager};
use tokio::time::{interval, Duration};

use crate::state::AppState;

pub async fn run(app: AppHandle) {
    let mut tick = interval(Duration::from_millis(500));
    let mut was_running = false;

    loop {
        tick.tick().await;

        let pid = tokio::task::spawn_blocking(soul_parser::find_tork_pid)
            .await
            .unwrap_or(None);

        let running = pid.is_some();

        // Always update pid_cache so poll_loop can read it
        {
            let state = app.state::<AppState>();
            let mut cache = state.pid_cache.lock().unwrap_or_else(|e| e.into_inner());
            *cache = pid;
        }

        if running != was_running {
            if running {
                if let Some(p) = pid {
                    let _ = app.emit("engine-started", serde_json::json!({ "pid": p }));
                }
            } else {
                let _ = app.emit("engine-stopped", serde_json::json!({}));
                let state = app.state::<AppState>();
                {
                    let mut cache = state.soul_cache.lock().unwrap_or_else(|e| e.into_inner());
                    *cache = None;
                }
            }
            was_running = running;
        }
    }
}
