//! Evolution commands — trigger evolution

use crate::common::project_root;
use serde::Serialize;

#[derive(Serialize)]
pub struct EvolutionResult {
    pub success: bool,
    pub output: String,
}

#[tauri::command]
pub async fn trigger_evolution() -> Result<EvolutionResult, String> {
    let root = project_root();

    let output = tokio::process::Command::new("python3")
        .arg(root.join("cloud/evolution.py"))
        .current_dir(&root)
        .output()
        .await
        .map_err(|e| format!("Failed to run evolution: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();

    Ok(EvolutionResult {
        success: output.status.success(),
        output: if output.status.success() {
            stdout
        } else {
            stderr
        },
    })
}
