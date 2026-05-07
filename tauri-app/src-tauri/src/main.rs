#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    // NVIDIA + webkit2gtk: 禁用 DMA-BUF 渲染，回退到软件渲染
    std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    tork_gui::run()
}
