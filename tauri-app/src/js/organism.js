// ── 有机体渲染 ── Metaball 形态 + 脉冲中心 + 脉络线 + 细胞膜

const state = window.state;

let canvas, ctx, W, H;
let noiseFn = null;

// Marching squares edge table (16 entries for 4 corner cases)
const EDGE_TABLE = [
  [],[[3,0]],[[0,1]],[[3,1]],
  [[1,2]],[[3,0],[1,2]],[[0,2]],[[3,2]],
  [[2,3]],[[2,0]],[[0,1],[2,3]],[[2,1]],
  [[1,3]],[[1,0]],[[0,3]],[]
];

const blobs = [
  { x: 0.5, y: 0.5, r: 0.18, targetX: 0.5, targetY: 0.5, targetR: 0.18, label: 'core' },
  { x: 0.35, y: 0.4, r: 0.10, targetX: 0.35, targetY: 0.4, targetR: 0.10, label: 'fear' },
  { x: 0.65, y: 0.4, r: 0.10, targetX: 0.65, targetY: 0.4, targetR: 0.10, label: 'desire' },
  { x: 0.45, y: 0.65, r: 0.09, targetX: 0.45, targetY: 0.65, targetR: 0.09, label: 'curiosity' },
  { x: 0.55, y: 0.35, r: 0.08, targetX: 0.55, targetY: 0.35, targetR: 0.08, label: 'tln' },
];

// Metaball 场值网格
const GRID_W = 80, GRID_H = 60;
const field = new Float32Array(GRID_W * GRID_H);
const THRESHOLD = 1.0;

// 粒子系统
const particles = [];
const MAX_PARTICLES = 60;

function initParticles() {
  particles.length = 0;
  for (let i = 0; i < MAX_PARTICLES; i++) {
    particles.push({
      x: Math.random(), y: Math.random(),
      vx: (Math.random() - 0.5) * 0.001,
      vy: (Math.random() - 0.5) * 0.001,
      life: Math.random(),
      size: 1 + Math.random() * 2,
    });
  }
}

const organism = {
  init() {
    canvas = document.getElementById('organism-canvas');
    if (!canvas) return;
    ctx = canvas.getContext('2d');
    this.resize();
    initParticles();

    // Lazy-load simplex-noise
    import('https://esm.sh/simplex-noise@4.0.3').then(mod => {
      noiseFn = mod.createNoise2D();
    }).catch(() => {
      // Fallback: sin-based pseudo-noise
      noiseFn = (x, y) => Math.sin(x * 1.7 + y * 0.3) * Math.cos(y * 2.1 - x * 0.5);
    });

    let resizeTimer;
    window.addEventListener('resize', () => {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(() => this.resize(), 100);
    });
  },

  resize() {
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    W = window.innerWidth;
    H = window.innerHeight;
    canvas.width = W * dpr;
    canvas.height = H * dpr;
    canvas.style.width = W + 'px';
    canvas.style.height = H + 'px';
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  },

  update(dt) {
    const d = state.display;
    const t = performance.now() * 0.001;

    // 本能驱动 blob 目标位置/半径
    const fearNorm = d.fear / 100;
    const desireNorm = d.desire / 100;
    const curiosityNorm = d.curiosity / 100;
    const tlnNorm = (d.tln_action + d.tln_modify + d.tln_explore + d.tln_energy) / 512;

    // Core: 脉动
    blobs[0].targetR = 0.15 + 0.04 * Math.sin(t * 1.2) + tlnNorm * 0.03;
    blobs[0].targetX = 0.5 + Math.sin(t * 0.3) * 0.02;
    blobs[0].targetY = 0.5 + Math.cos(t * 0.4) * 0.02;

    // Fear: 左上收缩
    blobs[1].targetR = 0.06 + fearNorm * 0.08;
    blobs[1].targetX = 0.35 - fearNorm * 0.05;
    blobs[1].targetY = 0.4 - fearNorm * 0.05;

    // Desire: 右上扩张
    blobs[2].targetR = 0.06 + desireNorm * 0.10;
    blobs[2].targetX = 0.65 + desireNorm * 0.03;
    blobs[2].targetY = 0.4 - desireNorm * 0.03;

    // Curiosity: 下方游走
    blobs[3].targetR = 0.05 + curiosityNorm * 0.08;
    blobs[3].targetX = 0.45 + Math.sin(t * 0.7) * curiosityNorm * 0.08;
    blobs[3].targetY = 0.65 + Math.cos(t * 0.5) * curiosityNorm * 0.05;

    // TLN: 顶部微动
    blobs[4].targetR = 0.04 + Math.abs(tlnNorm) * 0.06;
    blobs[4].targetX = 0.55 + Math.sin(t * 0.9) * 0.03;
    blobs[4].targetY = 0.35 + Math.cos(t * 0.6) * 0.02;

    // Noise 调制
    if (noiseFn) {
      for (let i = 0; i < blobs.length; i++) {
        const b = blobs[i];
        const nx = noiseFn(t * 0.5 + i * 10, 0) * 0.02;
        const ny = noiseFn(0, t * 0.5 + i * 10) * 0.02;
        b.targetX += nx;
        b.targetY += ny;
        b.targetR += noiseFn(t * 0.3 + i * 5, t * 0.2) * 0.01;
      }
    }

    // 指数平滑插值
    const factor = 1 - Math.exp(-dt * 3);
    for (const b of blobs) {
      b.x += (b.targetX - b.x) * factor;
      b.y += (b.targetY - b.y) * factor;
      b.r += (b.targetR - b.r) * factor;
    }

    // 粒子更新
    for (const p of particles) {
      p.x += p.vx;
      p.y += p.vy;
      p.life -= dt * 0.3;
      if (p.life <= 0 || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) {
        p.x = 0.5 + (Math.random() - 0.5) * 0.3;
        p.y = 0.5 + (Math.random() - 0.5) * 0.3;
        p.vx = (Math.random() - 0.5) * 0.001;
        p.vy = (Math.random() - 0.5) * 0.001;
        p.life = 0.5 + Math.random() * 0.5;
      }
    }
  },

  render() {
    if (!ctx || !W || !H) return;
    const { h, s, l } = accentHSL();

    // 半透明覆盖（拖尾效果）
    ctx.fillStyle = 'rgba(8, 8, 16, 0.12)';
    ctx.fillRect(0, 0, W, H);

    // 计算 metaball 场
    this.computeField();

    // 渲染细胞膜（marching squares 边缘线）
    this.renderMembrane(h, s, l);

    // 渲染填充区域
    this.renderFill(h, s, l);

    // 脉络线（连接 blob 中心）
    this.renderVeins(h, s, l);

    // 脉冲中心
    this.renderCore(h, s, l);

    // 粒子
    this.renderParticles(h, s, l);
  },

  computeField() {
    for (let gy = 0; gy < GRID_H; gy++) {
      for (let gx = 0; gx < GRID_W; gx++) {
        const px = (gx + 0.5) / GRID_W;
        const py = (gy + 0.5) / GRID_H;
        let val = 0;
        for (const b of blobs) {
          const dx = px - b.x;
          const dy = py - b.y;
          const dist2 = dx * dx + dy * dy;
          val += (b.r * b.r) / Math.max(dist2, 0.0001);
        }
        field[gy * GRID_W + gx] = val;
      }
    }
  },

  renderMembrane(h, s, l) {
    ctx.strokeStyle = `hsla(${h}, ${s}%, ${l + 10}%, 0.3)`;
    ctx.lineWidth = 1;

    for (let gy = 0; gy < GRID_H - 1; gy++) {
      for (let gx = 0; gx < GRID_W - 1; gx++) {
        const idx = gy * GRID_W + gx;
        const tl = field[idx] >= THRESHOLD ? 1 : 0;
        const tr = field[idx + 1] >= THRESHOLD ? 1 : 0;
        const br = field[idx + GRID_W + 1] >= THRESHOLD ? 1 : 0;
        const bl = field[idx + GRID_W] >= THRESHOLD ? 1 : 0;
        const ci = tl | (tr << 1) | (br << 2) | (bl << 3);
        if (ci === 0 || ci === 15) continue;

        const edges = EDGE_TABLE[ci];
        const x0 = (gx / GRID_W) * W;
        const y0 = (gy / GRID_H) * H;
        const cw = W / GRID_W;
        const ch = H / GRID_H;

        for (const [a, b] of edges) {
          ctx.beginPath();
          this.edgePoint(ctx, a, x0, y0, cw, ch);
          ctx.lineTo(...this.edgePointCoords(b, x0, y0, cw, ch));
          ctx.stroke();
        }
      }
    }
  },

  edgePointCoords(edge, x0, y0, cw, ch) {
    switch (edge) {
      case 0: return [x0 + cw / 2, y0];
      case 1: return [x0 + cw, y0 + ch / 2];
      case 2: return [x0 + cw / 2, y0 + ch];
      case 3: return [x0, y0 + ch / 2];
      default: return [x0, y0];
    }
  },

  edgePoint(ctx, edge, x0, y0, cw, ch) {
    const [x, y] = this.edgePointCoords(edge, x0, y0, cw, ch);
    ctx.moveTo(x, y);
  },

  renderFill(h, s, l) {
    // 径向渐变填充：每个 blob 画一个发光球
    ctx.globalCompositeOperation = 'lighter';
    for (const b of blobs) {
      const cx = b.x * W;
      const cy = b.y * H;
      const r = b.r * Math.min(W, H);
      const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
      grad.addColorStop(0, `hsla(${h}, ${s}%, ${l}%, 0.15)`);
      grad.addColorStop(0.6, `hsla(${h}, ${s}%, ${l * 0.5}%, 0.05)`);
      grad.addColorStop(1, 'rgba(0, 0, 0, 0)');
      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
  },

  renderVeins(h, s, l) {
    ctx.strokeStyle = `hsla(${h}, ${s * 0.6}%, ${l * 0.7}%, 0.12)`;
    ctx.lineWidth = 0.5;
    for (let i = 1; i < blobs.length; i++) {
      ctx.beginPath();
      ctx.moveTo(blobs[0].x * W, blobs[0].y * H);
      // 贝塞尔曲线
      const mx = (blobs[0].x + blobs[i].x) / 2 * W;
      const my = (blobs[0].y + blobs[i].y) / 2 * H + Math.sin(performance.now() * 0.001 + i) * 10;
      ctx.quadraticCurveTo(mx, my, blobs[i].x * W, blobs[i].y * H);
      ctx.stroke();
    }
  },

  renderCore(h, s, l) {
    const cx = blobs[0].x * W;
    const cy = blobs[0].y * H;
    const pulse = 0.5 + 0.5 * Math.sin(performance.now() * 0.003);
    const r = 3 + pulse * 4;

    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.fillStyle = `hsla(${h}, ${s}%, ${l + 20}%, ${0.3 + pulse * 0.4})`;
    ctx.shadowColor = `hsla(${h}, ${s}%, ${l}%, 0.6)`;
    ctx.shadowBlur = 12;
    ctx.fill();
    ctx.shadowBlur = 0;
  },

  renderParticles(h, s, l) {
    for (const p of particles) {
      const alpha = p.life * 0.4;
      if (alpha <= 0) continue;
      ctx.beginPath();
      ctx.arc(p.x * W, p.y * H, p.size, 0, Math.PI * 2);
      ctx.fillStyle = `hsla(${h}, ${s * 0.5}%, ${l + 10}%, ${alpha})`;
      ctx.fill();
    }
  }
};window.organism = organism;
