// ── 群体意识网络 ── 拓扑可视化 + 共识向量

const eventBus = window.eventBus;

let swarmCanvas, swarmCtx, swarmW, swarmH;
let _swarmNeedsResize = false;
let _peerList = [];
let _fetchInterval = null;

const swarm = {
  init() {
    swarmCanvas = document.getElementById('swarm-canvas');
    if (!swarmCanvas) return;
    swarmCtx = swarmCanvas.getContext('2d');
    this.resize();
    let resizeTimer;
    window.addEventListener('resize', () => {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(() => { _swarmNeedsResize = true; }, 100);
    });

    // 监听 peer-update 事件
    eventBus.listen('peer-update', (peer) => {
      const idx = _peerList.findIndex(p => p.node_id === peer.node_id);
      if (idx >= 0) {
        _peerList[idx] = peer;
      } else {
        _peerList.push(peer);
      }
      this.updatePeerCards();
    });

    // 监听 beacon-error 事件
    eventBus.listen('beacon-error', (err) => {
      console.warn('Beacon error:', err);
    });

    // 定期从后端拉取完整 peer 列表
    this.fetchPeers();
    _fetchInterval = setInterval(() => this.fetchPeers(), 5000);
  },

  resize() {
    if (!swarmCanvas) return false;
    const rect = swarmCanvas.parentElement.getBoundingClientRect();
    if (rect.width === 0) { _swarmNeedsResize = true; return false; }
    const dpr = window.devicePixelRatio || 1;
    swarmW = rect.width - 16;
    swarmH = 200;
    swarmCanvas.width = swarmW * dpr;
    swarmCanvas.height = swarmH * dpr;
    swarmCanvas.style.width = swarmW + 'px';
    swarmCanvas.style.height = swarmH + 'px';
    swarmCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
    _swarmNeedsResize = false;
    return true;
  },

  async fetchPeers() {
    if (!window.__TAURI__) return;
    try {
      const result = await window.__TAURI__.core.invoke('get_peers', {});
      // 重建列表：后端已清理过期 peer，前端应同步
      _peerList = [];
      if (result.self_node) {
        _peerList.push(result.self_node);
      }
      for (const peer of result.peers || []) {
        _peerList.push(peer);
      }
      this.updatePeerCards();
    } catch (e) {
      // 后端未就绪，静默忽略
    }
  },

  updatePeerCards() {
    const container = document.getElementById('peer-list');
    if (!container) return;
    container.innerHTML = '';
    if (_peerList.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'peer-empty';
      empty.textContent = '暂无群体节点';
      container.appendChild(empty);
      return;
    }
    for (const peer of _peerList) {
      const card = document.createElement('div');
      card.className = 'peer-card';
      const safeId = (peer.node_id || '').substring(0, 8).replace(/[<>&"']/g, '');
      card.innerHTML = `
        <div class="peer-id">${safeId}…</div>
        <div class="peer-info">tick: ${peer.tick || 0} gen: ${peer.gen_count || 0}</div>
      `;
      container.appendChild(card);
    }
  },

  render() {
    if (_swarmNeedsResize) this.resize();
    if (!swarmCtx || !swarmW) return;

    const { h, s, l } = accentHSL();

    // 清除
    swarmCtx.fillStyle = 'rgba(0, 0, 0, 0.1)';
    swarmCtx.fillRect(0, 0, swarmW, swarmH);

    if (_peerList.length === 0) {
      swarmCtx.fillStyle = 'rgba(255, 255, 255, 0.15)';
      swarmCtx.font = '12px monospace';
      swarmCtx.textAlign = 'center';
      swarmCtx.fillText('等待群体节点…', swarmW / 2, swarmH / 2);
      return;
    }

    const cx = swarmW / 2, cy = swarmH / 2;
    const orbitR = Math.min(swarmW, swarmH) * 0.3;

    // 轨道环
    swarmCtx.strokeStyle = `hsla(${h}, ${s * 0.3}%, ${l * 0.3}%, 0.15)`;
    swarmCtx.lineWidth = 1;
    swarmCtx.beginPath();
    swarmCtx.arc(cx, cy, orbitR, 0, Math.PI * 2);
    swarmCtx.stroke();

    // 节点
    for (let i = 0; i < _peerList.length; i++) {
      const angle = (i / _peerList.length) * Math.PI * 2 - Math.PI / 2;
      const nx = cx + Math.cos(angle) * orbitR;
      const ny = cy + Math.sin(angle) * orbitR;

      // 连接线到中心
      swarmCtx.strokeStyle = `hsla(${h}, ${s}%, ${l}%, 0.1)`;
      swarmCtx.lineWidth = 0.5;
      swarmCtx.beginPath();
      swarmCtx.moveTo(cx, cy);
      swarmCtx.lineTo(nx, ny);
      swarmCtx.stroke();

      // 节点光晕
      swarmCtx.beginPath();
      swarmCtx.arc(nx, ny, 8, 0, Math.PI * 2);
      swarmCtx.fillStyle = `hsla(${h}, ${s}%, ${l}%, 0.2)`;
      swarmCtx.fill();

      // 节点核心
      swarmCtx.beginPath();
      swarmCtx.arc(nx, ny, 4, 0, Math.PI * 2);
      swarmCtx.fillStyle = `hsla(${h}, ${s}%, ${l + 10}%, 0.6)`;
      swarmCtx.fill();

      // 脉冲
      const pulse = 0.5 + 0.5 * Math.sin(performance.now() * 0.002 + i);
      swarmCtx.beginPath();
      swarmCtx.arc(nx, ny, 4 + pulse * 4, 0, Math.PI * 2);
      swarmCtx.strokeStyle = `hsla(${h}, ${s}%, ${l}%, ${0.1 + pulse * 0.15})`;
      swarmCtx.lineWidth = 1;
      swarmCtx.stroke();
    }

    // 中心节点（自身）
    swarmCtx.beginPath();
    swarmCtx.arc(cx, cy, 6, 0, Math.PI * 2);
    swarmCtx.fillStyle = `hsla(${h}, ${s}%, ${l + 20}%, 0.5)`;
    swarmCtx.shadowColor = `hsla(${h}, ${s}%, ${l}%, 0.4)`;
    swarmCtx.shadowBlur = 8;
    swarmCtx.fill();
    swarmCtx.shadowBlur = 0;
  }
};window.swarm = swarm;
