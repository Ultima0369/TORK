//! Common utilities — shared project root resolution

use std::path::PathBuf;

/// Get project root directory.
/// Priority: TORK_ROOT env var > auto-detect from exe path > current dir
pub fn project_root() -> PathBuf {
    if let Ok(root) = std::env::var("TORK_ROOT") {
        let p = PathBuf::from(&root);
        if p.is_dir() {
            return p;
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        let mut dir = exe.parent();
        while let Some(d) = dir {
            if d.join("src/engine").is_dir() {
                return d.to_path_buf();
            }
            if d.file_name().is_some_and(|n| n == "tauri-app") {
                if let Some(parent) = d.parent() {
                    if parent.join("src/engine").is_dir() {
                        return parent.to_path_buf();
                    }
                }
            }
            dir = d.parent();
        }
    }
    std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."))
}
