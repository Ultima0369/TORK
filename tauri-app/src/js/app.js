// ── App 入口 ── 初始化 + 面板切换 + 事件监听

// 全局错误捕获
window.onerror = function(msg, url, line, col, err) {
  const d = document.createElement('div');
  d.style.cssText = 'position:fixed;top:0;left:0;right:0;background:red;color:white;padding:8px;font:12px monospace;z-index:99999;white-space:pre-wrap;';
  d.textContent = 'JS ERROR: ' + msg + ' (' + url + ':' + line + ')';
  document.body.appendChild(d);
  return false;
};

const state = window.state;
const eventBus = window.eventBus;
const organism = window.organism;
const vitals = window.vitals;
const tlnVisual = window.tlnVisual;
const colorEngine = window.colorEngine;
const animationLoop = window.animationLoop;

const PANEL_MAP = {
  'v': 'vitals-panel',
  't': 'tln-panel',
  'e': 'editor-panel',
  'c': 'chat-panel',
  's': 'swarm-panel',
};

function togglePanel(id) {
  const el = document.getElementById(id);
  if (!el) return;
  const isOpen = el.classList.contains('open');
  // 关闭所有面板
  document.querySelectorAll('.panel.open').forEach(p => p.classList.remove('open'));
  // 如果之前是关闭的，打开它
  if (!isOpen) {
    el.classList.add('open');
    // 面板打开后立即触发一帧渲染
    requestAnimationFrame(() => {
      if (id === 'vitals-panel' && vitals) vitals.update(0);
      if (id === 'tln-panel' && tlnVisual) { tlnVisual.update(0); tlnVisual.render(); }
    });
  }
}

async function initApp() {
  // 初始化各模块
  if (organism) organism.init();
  if (vitals) vitals.init();
  if (tlnVisual) tlnVisual.init();

  // 初始化事件总线
  if (eventBus) await eventBus.init();

  // 监听 Tauri 事件
  if (eventBus) {
    eventBus.listen('update', (payload) => eventBus.handleUpdate(payload));
    eventBus.listen('engine-started', () => { state.target.engine_running = true; });
    eventBus.listen('engine-stopped', () => {
      const t = state.target;
      t.engine_running = false;
      t.pid = null;
      t.mentor = null;
      t.dispatch = null;
    });
    eventBus.listen('peer-update', (payload) => {
      if (window.swarm && window.swarm.addPeer) window.swarm.addPeer(payload);
    });
  }

  // 键盘快捷键
  document.addEventListener('keydown', (e) => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
    const key = e.key.toLowerCase();
    if (PANEL_MAP[key]) {
      e.preventDefault();
      togglePanel(PANEL_MAP[key]);
    }
    if (e.key === 'Escape') {
      togglePanel('settings-panel');
    }
  });

  // 面板关闭按钮
  document.querySelectorAll('.panel-close').forEach(btn => {
    btn.addEventListener('click', () => {
      const panelId = btn.dataset.panel;
      if (panelId) {
        const panel = document.getElementById(panelId);
        if (panel) panel.classList.remove('open');
      }
    });
  });

  // 启动动画循环
  if (animationLoop) animationLoop.start();

  // 开机画面淡出
  setTimeout(() => {
    const splash = document.getElementById('splash');
    if (splash) {
      splash.classList.add('fade-out');
      splash.addEventListener('transitionend', () => splash.remove());
    }
  }, 1800);
}

initApp();
