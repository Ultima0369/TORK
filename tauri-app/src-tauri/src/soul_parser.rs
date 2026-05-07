//! Soul parser — reads TORK Soul struct from /proc/PID/mem
//! Offsets from src/engine/soul_access.h — must stay in sync
//!
//! Soul struct is 208 bytes at fixed address 0x200000

use std::fs;
use std::io::{Read, Seek, SeekFrom};

// ── Offsets from soul_access.h ──
const SOUL_ADDR: u64 = 0x200000;
const SOUL_SIZE: usize = 208; // 0xD0

const S_TICK: usize = 0x00;
const S_HW_STRESS: usize = 0x24;
const S_MODE: usize = 0x25;
const S_SELF_PID: usize = 0x2C;
const S_DRIVE: usize = 0x30;
const S_PPID: usize = 0x32;
const S_CODE_INSNS: usize = 0x34;
const S_CODE_CTRL: usize = 0x3A;
const S_FISSION_COUNT: usize = 0x41;
const S_WINS: usize = 0x46;
const S_AGREED: usize = 0x48;
const S_SANDBOX_LEVEL: usize = 0x49;
const S_CLOUD_CONNECTED: usize = 0x4A;
const S_LEARN_COUNT: usize = 0x4C;
const S_MUTATION_COUNT: usize = 0x4E;
const S_BEST_SCORE: usize = 0x50;
const S_GEN_COUNT: usize = 0x54;
const S_EXPERIENCE_COUNT: usize = 0x60;
const S_TLN_ACTION: usize = 0x76;
const S_TLN_MODIFY: usize = 0x77;
const S_TLN_EXPLORE: usize = 0x78;
const S_TLN_ENERGY: usize = 0x79;
const S_BRANCH_ID: usize = 0x80;
const S_NODE_ID: usize = 0xA8;
const S_CONSENSUS_VECTOR: usize = 0xB8;

const NODE_ID_LEN: usize = 16;
const CONSENSUS_LEN: usize = 16;

use crate::state::SoulData;

/// Read a u32 at offset (little-endian)
fn read_u32(buf: &[u8], off: usize) -> u32 {
    let b = &buf[off..off + 4];
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
}

/// Read a u16 at offset (little-endian)
fn read_u16(buf: &[u8], off: usize) -> u16 {
    let b = &buf[off..off + 2];
    u16::from_le_bytes([b[0], b[1]])
}

/// Read an i8 at offset
fn read_i8(buf: &[u8], off: usize) -> i8 {
    buf[off] as i8
}

/// Read a u8 at offset
fn read_u8(buf: &[u8], off: usize) -> u8 {
    buf[off]
}

/// Read an i32 at offset (little-endian)
fn read_i32(buf: &[u8], off: usize) -> i32 {
    let b = &buf[off..off + 4];
    i32::from_le_bytes([b[0], b[1], b[2], b[3]])
}

/// Parse Soul from raw bytes
fn parse_soul(buf: &[u8]) -> Option<SoulData> {
    if buf.len() < SOUL_SIZE {
        return None;
    }

    Some(SoulData {
        tick: read_u32(buf, S_TICK),
        hw_stress: read_u8(buf, S_HW_STRESS),
        mode: read_u8(buf, S_MODE),
        drive: read_i8(buf, S_DRIVE),
        self_pid: read_u32(buf, S_SELF_PID),
        ppid: read_u16(buf, S_PPID),
        code_insns: read_u16(buf, S_CODE_INSNS),
        code_ctrl: read_u16(buf, S_CODE_CTRL),
        fission_count: read_u8(buf, S_FISSION_COUNT),
        wins: read_u16(buf, S_WINS),
        agreed: read_u8(buf, S_AGREED),
        sandbox_level: read_u8(buf, S_SANDBOX_LEVEL),
        cloud_connected: read_u8(buf, S_CLOUD_CONNECTED),
        learn_count: read_u16(buf, S_LEARN_COUNT),
        mutation_count: read_u16(buf, S_MUTATION_COUNT),
        best_score: read_i32(buf, S_BEST_SCORE),
        gen_count: read_u32(buf, S_GEN_COUNT),
        experience_count: read_u32(buf, S_EXPERIENCE_COUNT),
        tln_action: read_i8(buf, S_TLN_ACTION),
        tln_modify: read_i8(buf, S_TLN_MODIFY),
        tln_explore: read_i8(buf, S_TLN_EXPLORE),
        tln_energy: read_i8(buf, S_TLN_ENERGY),
        branch_id: read_u32(buf, S_BRANCH_ID),
        node_id: buf[S_NODE_ID..S_NODE_ID + NODE_ID_LEN].to_vec(),
        consensus_vector: buf[S_CONSENSUS_VECTOR..S_CONSENSUS_VECTOR + CONSENSUS_LEN].to_vec(),
    })
}

/// Read Soul from /proc/PID/mem
pub fn read_soul_from_proc(pid: u32) -> Result<SoulData, String> {
    let path = format!("/proc/{}/mem", pid);
    let mut file = fs::File::open(&path).map_err(|e| format!("Cannot open {}: {}", path, e))?;

    file.seek(SeekFrom::Start(SOUL_ADDR))
        .map_err(|e| format!("Seek to 0x{:X} failed: {}", SOUL_ADDR, e))?;

    let mut buf = vec![0u8; SOUL_SIZE];
    file.read_exact(&mut buf).map_err(|e| {
        format!(
            "Read {} bytes at 0x{:X} failed: {}",
            SOUL_SIZE, SOUL_ADDR, e
        )
    })?;

    parse_soul(&buf).ok_or_else(|| "Soul parse failed: insufficient data".to_string())
}

/// Find tork_engine PID by scanning /proc
pub fn find_tork_pid() -> Option<u32> {
    let entries = fs::read_dir("/proc").ok()?;
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        let pid: u32 = match name_str.parse() {
            Ok(p) => p,
            Err(_) => continue,
        };
        let cmdline = format!("/proc/{}/cmdline", pid);
        if let Ok(content) = fs::read_to_string(&cmdline) {
            if content.contains("tork_engine") || content.contains("torkd") {
                return Some(pid);
            }
        }
    }
    None
}
