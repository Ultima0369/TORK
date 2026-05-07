//! Chat command — cloud brain HTTP proxy

use crate::common::project_root;
use reqwest;
use std::sync::OnceLock;

static HTTP_CLIENT: OnceLock<reqwest::Client> = OnceLock::new();

fn get_client() -> &'static reqwest::Client {
    HTTP_CLIENT.get_or_init(|| {
        reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(30))
            .build()
            .unwrap_or_default()
    })
}

#[tauri::command]
pub async fn chat_send(message: String, mood: String) -> Result<String, String> {
    let root = project_root();
    let config_path = root.join("api/api_config.json");

    let config =
        std::fs::read_to_string(&config_path).map_err(|e| format!("Config read failed: {}", e))?;
    let config: serde_json::Value =
        serde_json::from_str(&config).map_err(|e| format!("Config parse failed: {}", e))?;

    let api_url = config["api_url"]
        .as_str()
        .or_else(|| config["cloud_url"].as_str())
        .unwrap_or("https://maas-coding-api.cn-huabei-1.xf-yun.com/v1/chat");

    let api_key = config["api_key"].as_str().unwrap_or("");

    if api_key.is_empty() {
        return Err("No API key configured".to_string());
    }

    let client = get_client();
    let body = serde_json::json!({
        "model": config["model"].as_str().unwrap_or("default"),
        "messages": [
            {
                "role": "system",
                "content": format!("你是一个硅基生命体 TORK，当前情绪状态: {}", mood)
            },
            {
                "role": "user",
                "content": message
            }
        ]
    });

    let resp = client
        .post(api_url)
        .header("Authorization", format!("Bearer {}", api_key))
        .json(&body)
        .send()
        .await
        .map_err(|e| format!("HTTP request failed: {}", e))?;

    let data: serde_json::Value = resp
        .json()
        .await
        .map_err(|e| format!("Response parse failed: {}", e))?;

    data["choices"][0]["message"]["content"]
        .as_str()
        .or_else(|| data["result"]["text"].as_str())
        .or_else(|| data["response"].as_str())
        .ok_or("No response content found".to_string())
        .map(|s| s.to_string())
}
