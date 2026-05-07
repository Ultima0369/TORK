mod background;
mod commands;
pub mod common;
mod soul_parser;
mod state;
mod torkd_bridge;

pub fn run() {
    tauri::Builder::default()
        .manage(state::AppState::default())
        .setup(|app| {
            let app_handle = app.handle().clone();
            tauri::async_runtime::spawn(background::poll_loop::run(app_handle.clone()));
            tauri::async_runtime::spawn(background::proc_watcher::run(app_handle.clone()));
            tauri::async_runtime::spawn(background::beacon_listener::run(app_handle.clone()));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::socket::torkd_query,
            commands::file_ops::read_file,
            commands::file_ops::write_file,
            commands::file_ops::list_dir,
            commands::chat::chat_send,
            commands::evolution::trigger_evolution,
            commands::beacon::get_peers,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
