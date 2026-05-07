use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::sync::Mutex;

/// Serialize Vec<u8> as hex string instead of number array
fn serialize_hex<S: Serializer>(v: &[u8], s: S) -> Result<S::Ok, S::Error> {
    let hex: String = v.iter().map(|b| format!("{:02x}", b)).collect();
    s.serialize_str(&hex)
}

/// Deserialize hex string back to Vec<u8>
fn deserialize_hex<'de, D: Deserializer<'de>>(d: D) -> Result<Vec<u8>, D::Error> {
    let hex = String::deserialize(d)?;
    (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).map_err(serde::de::Error::custom))
        .collect()
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct SoulData {
    pub tick: u32,
    pub hw_stress: u8,
    pub mode: u8,
    pub drive: i8,
    pub self_pid: u32,
    pub ppid: u16,
    pub code_insns: u16,
    pub code_ctrl: u16,
    pub fission_count: u8,
    pub wins: u16,
    pub agreed: u8,
    pub sandbox_level: u8,
    pub cloud_connected: u8,
    pub learn_count: u16,
    pub mutation_count: u16,
    pub gen_count: u32,
    pub experience_count: u32,
    pub tln_action: i8,
    pub tln_modify: i8,
    pub tln_explore: i8,
    pub tln_energy: i8,
    pub best_score: i32,
    pub branch_id: u32,
    #[serde(serialize_with = "serialize_hex", deserialize_with = "deserialize_hex")]
    pub node_id: Vec<u8>,
    #[serde(serialize_with = "serialize_hex", deserialize_with = "deserialize_hex")]
    pub consensus_vector: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct InstinctVector {
    pub fear: f64,
    pub desire: f64,
    pub curiosity: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UpdatePayload {
    pub soul: Option<SoulData>,
    pub instincts: Option<InstinctVector>,
    pub engine_running: bool,
    pub pid: Option<u32>,
    pub mentor: Option<String>,
    pub dispatch: Option<String>,
}

pub struct AppState {
    pub soul_cache: Mutex<Option<SoulData>>,
    pub pid_cache: Mutex<Option<u32>>,
    pub peer_cache: Mutex<Vec<PeerEntry>>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct PeerEntry {
    pub node_id: String,
    pub tick: u32,
    pub gen_count: u32,
    pub drive: i8,
    pub last_seen: u64,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            soul_cache: Mutex::new(None),
            pid_cache: Mutex::new(None),
            peer_cache: Mutex::new(Vec::new()),
        }
    }
}

/// Derive instincts from Soul state
pub fn derive_instincts(soul: &SoulData) -> InstinctVector {
    let hs = soul.hw_stress as f64;
    let mode = soul.mode as i32;

    let fear = (hs * 30.0 + if mode == 2 { 10.0 } else { 0.0 }).clamp(0.0, 100.0);
    let desire = (70.0 - hs * 15.0 + if mode == 1 { 10.0 } else { 0.0 }).clamp(0.0, 100.0);
    let curiosity = (55.0 - hs * 8.0 + if mode == 0 { 15.0 } else { 0.0 }).clamp(0.0, 100.0);

    InstinctVector {
        fear,
        desire,
        curiosity,
    }
}
