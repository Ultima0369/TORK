//! Poll loop — 1s interval: read Soul + derive instincts + emit("update")
//! Reads PID from AppState (maintained by proc_watcher)
//! Uses spawn_blocking for synchronous I/O operations

use crate::soul_parser;
use crate::state;
use tauri::{AppHandle, Emitter, Manager};
use tokio::time::{interval, Duration};

pub async fn run(app: AppHandle) {
    let mut tick = interval(Duration::from_secs(1));
    let mut error_count: u32 = 0;

    loop {
        tick.tick().await;

        // Get PID from proc_watcher's cache
        let pid = {
            let st = app.state::<state::AppState>();
            let cache = st.pid_cache.lock().unwrap_or_else(|e| e.into_inner());
            *cache
        };

        let Some(pid) = pid else { continue };

        // Read Soul from /proc/PID/mem (blocking I/O → spawn_blocking)
        let soul = match tokio::task::spawn_blocking(move || soul_parser::read_soul_from_proc(pid))
            .await
        {
            Ok(Ok(s)) => {
                error_count = 0;
                s
            }
            Ok(Err(e)) => {
                // Log only first 3 failures, then every 30th
                error_count += 1;
                if error_count <= 3 || error_count.is_multiple_of(30) {
                    eprintln!("Soul read failed ({}): {}", error_count, e);
                }
                continue;
            }
            Err(e) => {
                eprintln!("spawn_blocking panicked: {}", e);
                continue;
            }
        };

        // Derive instinct vector (CPU-only, no blocking)
        let instincts = state::derive_instincts(&soul);

        // Query torkd for mentor/dispatch (blocking Unix Socket → spawn_blocking)
        // These are independent queries — dispatch should be queried regardless of mentor result
        let (mentor, dispatch) = tokio::task::spawn_blocking(move || {
            let m = crate::torkd_bridge::query("mentor").ok();
            let d = crate::torkd_bridge::query("dispatch").ok();
            (m, d)
        })
        .await
        .unwrap_or((None, None));

        // Update soul cache
        {
            let st = app.state::<state::AppState>();
            let mut cache = st.soul_cache.lock().unwrap_or_else(|e| e.into_inner());
            *cache = Some(soul.clone());
        }

        // Emit update event
        let payload = state::UpdatePayload {
            engine_running: true,
            pid: Some(pid),
            soul: Some(soul),
            instincts: Some(instincts),
            mentor,
            dispatch,
        };

        if let Err(e) = app.emit("update", &payload) {
            eprintln!("Emit update failed: {}", e);
        }
    }
}
