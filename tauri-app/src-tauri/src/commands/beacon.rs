//! Beacon commands — peer discovery and consensus

use crate::state::{AppState, PeerEntry};
use serde::Serialize;
use tauri::Manager;

#[derive(Serialize)]
pub struct PeersResult {
    pub self_node: Option<PeerInfo>,
    pub peers: Vec<PeerInfo>,
}

#[derive(Serialize, Clone)]
pub struct PeerInfo {
    pub node_id: String,
    pub tick: u32,
    pub gen_count: u32,
    pub drive: i8,
}

impl From<&PeerEntry> for PeerInfo {
    fn from(entry: &PeerEntry) -> Self {
        PeerInfo {
            node_id: entry.node_id.clone(),
            tick: entry.tick,
            gen_count: entry.gen_count,
            drive: entry.drive,
        }
    }
}

#[tauri::command]
pub fn get_peers(app: tauri::AppHandle) -> Result<PeersResult, String> {
    let state = app.state::<AppState>();
    let cache = state.peer_cache.lock().unwrap_or_else(|e| e.into_inner());

    let peers: Vec<PeerInfo> = cache.iter().map(PeerInfo::from).collect();

    // Self node from soul cache — full 16-byte node_id
    let self_node = state
        .soul_cache
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .as_ref()
        .map(|soul| {
            let id: String = soul
                .node_id
                .iter()
                .map(|b| format!("{:02x}", b))
                .collect::<Vec<_>>()
                .join("");
            PeerInfo {
                node_id: id,
                tick: soul.tick,
                gen_count: soul.gen_count,
                drive: soul.drive,
            }
        });

    Ok(PeersResult { self_node, peers })
}
