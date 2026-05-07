//! Beacon listener — UDP multicast listener for peer discovery
//! Listens on 239.42.69.42:42069, parses beacon_frame_t (52 bytes)
//! Emits "peer-update" events and updates AppState peer_cache
//!
//! beacon_frame_t layout (from beacon.h):
//!   0x00: magic        uint32  BEACON_MAGIC = 0x544F524B
//!   0x04: tick         uint32  Soul S_TICK
//!   0x08: crc32_prefix uint32  Soul CRC32
//!   0x0C: tsc_lo       uint32  rdtsc low 32 bits
//!   0x10: pi_digest    uint8[16]
//!   0x20: heartbeat_ms uint16
//!   0x22: node_id      uint8[16]
//!   0x32: _reserved    uint16

use crate::state::{AppState, PeerEntry};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::{AppHandle, Emitter, Manager};

const MULTICAST_ADDR: Ipv4Addr = Ipv4Addr::new(239, 42, 69, 42);
const MULTICAST_PORT: u16 = 42069;
const BEACON_SIZE: usize = 52;
const BEACON_MAGIC: u32 = 0x544F524B; // "TORK" (uppercase, per beacon.h)
const PEER_MAX: usize = 32;

fn parse_beacon(buf: &[u8]) -> Option<PeerEntry> {
    if buf.len() < BEACON_SIZE {
        return None;
    }

    let magic = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
    if magic != BEACON_MAGIC {
        return None;
    }

    // tick at 0x04
    let tick = u32::from_le_bytes([buf[0x04], buf[0x05], buf[0x06], buf[0x07]]);

    // node_id at 0x22, 16 bytes → full hex string (32 chars)
    let node_id = &buf[0x22..0x32];
    let mut id_hex = String::with_capacity(32);
    for &b in node_id {
        use std::fmt::Write;
        write!(&mut id_hex, "{:02x}", b).unwrap();
    }

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    Some(PeerEntry {
        node_id: id_hex,
        tick,
        gen_count: 0, // beacon frame has no gen_count
        drive: 0,     // beacon frame has no drive
        last_seen: now,
    })
}

pub async fn run(app: AppHandle) {
    let socket =
        match tokio::net::UdpSocket::bind(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, MULTICAST_PORT))
            .await
        {
            Ok(s) => s,
            Err(e) => {
                eprintln!("Beacon: bind failed: {}", e);
                let _ = app.emit("beacon-error", &format!("bind failed: {}", e));
                return;
            }
        };

    if let Err(e) = socket.join_multicast_v4(MULTICAST_ADDR, Ipv4Addr::UNSPECIFIED) {
        eprintln!(
            "Beacon: multicast join failed: {} (may work on loopback)",
            e
        );
    }

    let mut buf = [0u8; 256];

    loop {
        match socket.recv_from(&mut buf).await {
            Ok((len, _addr)) => {
                if len >= BEACON_SIZE {
                    if let Some(peer) = parse_beacon(&buf[..len]) {
                        let now = SystemTime::now()
                            .duration_since(UNIX_EPOCH)
                            .unwrap_or_default()
                            .as_secs();

                        {
                            let state = app.state::<AppState>();
                            let mut cache =
                                state.peer_cache.lock().unwrap_or_else(|e| e.into_inner());
                            if let Some(existing) =
                                cache.iter_mut().find(|p| p.node_id == peer.node_id)
                            {
                                *existing = peer.clone();
                            } else if cache.len() < PEER_MAX {
                                cache.push(peer.clone());
                            }
                            // Prune stale peers (>30s)
                            cache.retain(|p| now - p.last_seen < 30);
                        }

                        let _ = app.emit("peer-update", &peer);
                    }
                }
            }
            Err(e) => {
                eprintln!("Beacon: recv error: {}", e);
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
            }
        }
    }
}
