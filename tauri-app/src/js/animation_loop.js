//! Animation loop — single rAF driving all rendering

const state = window.state;
const organism = window.organism;
const vitals = window.vitals;
const tlnVisual = window.tlnVisual;
const colorEngine = window.colorEngine;

window.animationLoop = {
  running: false,
  lastTime: 0,

  start() {
    if (this.running) return;
    this.running = true;
    this.lastTime = performance.now();
    const loop = (now) => {
      if (!this.running) return;
      const dt = Math.min((now - this.lastTime) / 1000, 0.1);
      this.lastTime = now;
      this.tick(dt);
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  },

  stop() { this.running = false; },

  tick(dt) {
    state.interpolate(dt);

    // 有机体始终渲染
    if (organism) { organism.update(dt); organism.render(); }

    // 状态栏始终更新
    if (vitals) vitals.updateStatusBar();

    // vitals 面板内容只在面板打开时渲染
    const vp = document.getElementById('vitals-panel');
    if (vp && vp.classList.contains('open') && vitals) {
      vitals.update(dt);
    }

    // TLN 面板只在打开时渲染
    const tp = document.getElementById('tln-panel');
    if (tp && tp.classList.contains('open') && tlnVisual) {
      tlnVisual.update(dt);
      tlnVisual.render();
    }

    // 颜色引擎始终运行
    if (colorEngine) {
      const { h, s, l } = colorEngine.computeAccentColor(state.display);
      colorEngine.applyColor(h, s, l);
    }
  },
};