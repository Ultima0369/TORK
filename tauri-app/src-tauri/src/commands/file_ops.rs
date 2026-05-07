//! File operations — read, write, list directory
//! All paths are relative to the project root with strict security

use crate::common::project_root;
use serde::Serialize;
use std::path::PathBuf;

fn resolve_path(path: &str) -> Result<PathBuf, String> {
    if path.contains("..") {
        return Err("Path traversal denied: .. not allowed".to_string());
    }

    let root = project_root();
    let resolved = if path.starts_with('/') {
        root.join(path.strip_prefix('/').unwrap())
    } else {
        root.join(path)
    };

    match resolved.canonicalize() {
        Ok(canonical) => {
            let root_canonical = match root.canonicalize() {
                Ok(c) => c,
                Err(_) => root,
            };
            if !canonical.starts_with(&root_canonical) {
                return Err("Path traversal denied".to_string());
            }
            Ok(canonical)
        }
        Err(_) => {
            if let Some(parent) = resolved.parent() {
                if let Ok(parent_canonical) = parent.canonicalize() {
                    let root_canonical = match root.canonicalize() {
                        Ok(c) => c,
                        Err(_) => root,
                    };
                    if !parent_canonical.starts_with(&root_canonical) {
                        return Err("Path traversal denied".to_string());
                    }
                }
            }
            Ok(resolved)
        }
    }
}

#[derive(Serialize)]
pub struct DirEntry {
    pub name: String,
    pub is_dir: bool,
    pub size: u64,
}

#[tauri::command]
pub fn read_file(path: String) -> Result<String, String> {
    let resolved = resolve_path(&path)?;
    std::fs::read_to_string(&resolved).map_err(|e| format!("Read failed: {}", e))
}

#[tauri::command]
pub fn write_file(path: String, content: String) -> Result<(), String> {
    let resolved = resolve_path(&path)?;
    if let Some(parent) = resolved.parent() {
        std::fs::create_dir_all(parent).map_err(|e| format!("Create dir failed: {}", e))?;
    }
    std::fs::write(&resolved, content).map_err(|e| format!("Write failed: {}", e))
}

#[tauri::command]
pub fn list_dir(path: String) -> Result<Vec<DirEntry>, String> {
    let resolved = resolve_path(&path)?;
    let entries = std::fs::read_dir(&resolved).map_err(|e| format!("List dir failed: {}", e))?;

    let mut result = Vec::with_capacity(32);
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        let meta = match entry.metadata() {
            Ok(m) => m,
            Err(_) => continue,
        };
        result.push(DirEntry {
            name,
            is_dir: meta.is_dir(),
            size: meta.len(),
        });
    }
    result.sort_by(|a, b| b.is_dir.cmp(&a.is_dir).then(a.name.cmp(&b.name)));
    Ok(result)
}
