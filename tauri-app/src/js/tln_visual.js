// ── TLN 三值网络可视化 ── 4节点力导向图

const state = window.state;

let tlnCanvas, tlnCtx, tlnW, tlnH;
let _needsResize = false;

const TLN_NODES = [
  { id: 'action',  label: '行动', x: 0, y: 0, vx: 0, vy: 0 },
  { id: 'modify',  label: '修改', x: 0, y: 0, vx: 0, vy: 0 },
  { id: 'explore', label: '探索', x: 0, y: 0, vx: 0, vy: 0 },
  { id: 'energy',  label: '能量', x: 0, y: 0, vx: 0, vy: 0 },
];

// 逻辑连接：action→modify→explore→energy→action
const TLN_EDGES = [
  [0, 1], [1, 2], [2, 3], [3, 0],
];

const tln = {
  init() {
    tlnCanvas = document.getElementById('tln-canvas');
    if (!tlnCanvas) return;
    tlnCtx = tlnCanvas.getContext('2d');
    this.resize();
    let resizeTimer;
    window.addEventListener('resize', () => {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(() => { _needsResize = true; }, 100);
    });
  },

  resize() {
    if (!tlnCanvas) return false;
    const rect = tlnCanvas.parentElement.getBoundingClientRect();
    if (rect.width === 0) { _needsResize = true; return false; }
    const dpr = window.devicePixelRatio || 1;
    tlnW = rect.width - 16;
    tlnH = 200;
    tlnCanvas.width = tlnW * dpr;
    tlnCanvas.height = tlnH * dpr;
    tlnCanvas.style.width = tlnW + 'px';
    tlnCanvas.style.height = tlnH + 'px';
    tlnCtx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // 初始化节点位置
    const cx = tlnW / 2, cy = tlnH / 2, r = Math.min(tlnW, tlnH) * 0.3;
    for (let i = 0; i < TLN_NODES.length; i++) {
      const angle = (i / TLN_NODES.length) * Math.PI * 2 - Math.PI / 2;
      TLN_NODES[i].x = cx + Math.cos(angle) * r;
      TLN_NODES[i].y = cy + Math.sin(angle) * r;
    }
    _needsResize = false;
    return true;
  },

  update(dt) {
    // 简单力导向：节点向目标位置缓慢移动
    if (!tlnW) return;
    const cx = tlnW / 2, cy = tlnH / 2, r = Math.min(tlnW, tlnH) * 0.3;
    for (let i = 0; i < TLN_NODES.length; i++) {
      const angle = (i / TLN_NODES.length) * Math.PI * 2 - Math.PI / 2;
      const tx = cx + Math.cos(angle) * r;
      const ty = cy + Math.sin(angle) * r;
      TLN_NODES[i].x += (tx - TLN_NODES[i].x) * 0.02;
      TLN_NODES[i].y += (ty - TLN_NODES[i].y) * 0.02;
    }
  },

  render() {
    if (_needsResize) this.resize();
    if (!tlnCtx || !tlnW) return;

    const d = state.display;
    const values = [d.tln_action, d.tln_modify, d.tln_explore, d.tln_energy];
    const { h, s, l } = accentHSL();

    // 清除
    tlnCtx.fillStyle = 'rgba(0, 0, 0, 0.1)';
    tlnCtx.fillRect(0, 0, tlnW, tlnH);

    // 边
    for (const [a, b] of TLN_EDGES) {
      const na = TLN_NODES[a], nb = TLN_NODES[b];
      const va = values[a], vb = values[b];
      const active = Math.abs(va) > 5 || Math.abs(vb) > 5;

      tlnCtx.strokeStyle = active
        ? `hsla(${h}, ${s}%, ${l}%, 0.4)`
        : 'rgba(255, 255, 255, 0.05)';
      tlnCtx.lineWidth = active ? 1.5 : 0.5;
      tlnCtx.beginPath();
      tlnCtx.moveTo(na.x, na.y);
      tlnCtx.lineTo(nb.x, nb.y);
      tlnCtx.stroke();

      // 脉冲动画：活跃边上有流动的光点
      if (active) {
        const t = (performance.now() * 0.001) % 1;
        const px = na.x + (nb.x - na.x) * t;
        const py = na.y + (nb.y - na.y) * t;
        tlnCtx.beginPath();
        tlnCtx.arc(px, py, 2, 0, Math.PI * 2);
        tlnCtx.fillStyle = `hsla(${h}, ${s}%, ${l + 20}%, 0.6)`;
        tlnCtx.fill();
      }
    }

    // 节点
    for (let i = 0; i < TLN_NODES.length; i++) {
      const n = TLN_NODES[i];
      const v = values[i];
      const norm = v / 127; // -1 to 1

      let nodeH, nodeS, nodeL;
      if (norm > 0.05) {
        // 正值 → 绿色脉冲
        nodeH = 140; nodeS = 70; nodeL = 40 + norm * 20;
      } else if (norm < -0.05) {
        // 负值 → 红色脉冲
        nodeH = 0; nodeS = 70; nodeL = 40 + Math.abs(norm) * 20;
      } else {
        // 零 → 灰色悬置
        nodeH = 0; nodeS = 0; nodeL = 30;
      }

      const radius = 12 + Math.abs(norm) * 8;

      // 光晕
      tlnCtx.beginPath();
      tlnCtx.arc(n.x, n.y, radius + 6, 0, Math.PI * 2);
      tlnCtx.fillStyle = `hsla(${nodeH}, ${nodeS}%, ${nodeL}%, 0.1)`;
      tlnCtx.fill();

      // 节点圆
      tlnCtx.beginPath();
      tlnCtx.arc(n.x, n.y, radius, 0, Math.PI * 2);
      tlnCtx.fillStyle = `hsla(${nodeH}, ${nodeS}%, ${nodeL}%, 0.6)`;
      tlnCtx.fill();
      tlnCtx.strokeStyle = `hsla(${nodeH}, ${nodeS}%, ${nodeL + 20}%, 0.4)`;
      tlnCtx.lineWidth = 1;
      tlnCtx.stroke();

      // 标签
      tlnCtx.fillStyle = 'rgba(255, 255, 255, 0.7)';
      tlnCtx.font = '10px monospace';
      tlnCtx.textAlign = 'center';
      tlnCtx.fillText(n.label, n.x, n.y + radius + 14);

      // 值
      tlnCtx.fillStyle = `hsla(${nodeH}, ${nodeS}%, ${nodeL + 20}%, 0.8)`;
      tlnCtx.fillText(Math.round(v), n.x, n.y + 4);
    }
  }
};window.tlnVisual = tln;
