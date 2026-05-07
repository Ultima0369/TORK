//! Vitals panel — ECG waveform, instinct bars, soul fields

const state = window.state;

const SOUL_DISPLAY = [
  ['tick', 'Tick'], ['mode', 'Mode'], ['drive', 'Drive'],
  ['hw_stress', 'HW Stress'], ['self_pid', 'PID'], ['ppid', 'PPID'],
  ['code_insns', 'Insns'], ['code_ctrl', 'Ctrl'], ['fission_count', 'Fission'],
  ['wins', 'Wins'], ['agreed', 'Agreed'], ['sandbox_level', 'Sandbox'],
  ['cloud_connected', 'Cloud'], ['learn_count', 'Learn'],
  ['mutation_count', 'Mutation'], ['best_score', 'Best'],
  ['gen_count', 'Gen'], ['experience_count', 'Exp'], ['branch_id', 'Branch'],
];

const ECG_LEN = 200;
const ECG_CHANNELS = 3;

const vitals = {
  ecgData: [],
  ecgCanvas: null,
  ecgCtx: null,
  ecgScanPos: 0,
  _lastFieldsHash: '',

  init() {
    this.ecgCanvas = document.getElementById('ecg-canvas');
    if (this.ecgCanvas) {
      this.ecgCtx = this.ecgCanvas.getContext('2d');
    }
    for (let ch = 0; ch < ECG_CHANNELS; ch++) {
      this.ecgData[ch] = new Float32Array(ECG_LEN);
    }
  },

  setBar(name, val) {
    const bar = document.getElementById('bar-' + name);
    const valEl = document.getElementById('val-' + name);
    if (bar) bar.style.width = Math.min(100, Math.max(0, val * 100)) + '%';
    if (valEl) valEl.textContent = val.toFixed(2);
  },

  update(dt) {
    const d = state.display;
    this.setBar('fear', Math.abs(d.fear || 0) / 100);
    this.setBar('desire', Math.abs(d.desire || 0) / 100);
    this.setBar('curiosity', Math.abs(d.curiosity || 0) / 100);
    this.drawEcg();
    this.updateSoulFields();
  },

  drawEcg() {
    const canvas = this.ecgCanvas;
    const ctx = this.ecgCtx;
    if (!canvas || !ctx) return;

    const rect = canvas.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) return;
    const pw = Math.floor(rect.width);
    const ph = Math.floor(rect.height);
    if (canvas.width !== pw || canvas.height !== ph) {
      canvas.width = pw;
      canvas.height = ph;
    }

    const d = state.display;
    const w = canvas.width;
    const h = canvas.height;
    const ch = h / ECG_CHANNELS;

    this.ecgScanPos = (this.ecgScanPos + 1) % ECG_LEN;
    this.ecgData[0][this.ecgScanPos] = (d.tick || 0) % 100 / 100;
    this.ecgData[1][this.ecgScanPos] = (d.drive || 0) / 128;
    this.ecgData[2][this.ecgScanPos] = ((d.tln_action || 0) + (d.tln_modify || 0) + (d.tln_explore || 0)) / 3 / 128;

    ctx.fillStyle = 'rgba(0,0,0,0.15)';
    ctx.fillRect(0, 0, w, h);

    const colors = ['#00ff88', '#ff8800', '#8844ff'];
    for (let ci = 0; ci < ECG_CHANNELS; ci++) {
      ctx.strokeStyle = colors[ci];
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let i = 0; i < ECG_LEN; i++) {
        const x = (i / ECG_LEN) * w;
        const y = ch * ci + ch / 2 - this.ecgData[ci][i] * ch * 0.4;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    const scanX = (this.ecgScanPos / ECG_LEN) * w;
    ctx.strokeStyle = 'rgba(255,255,255,0.3)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(scanX, 0);
    ctx.lineTo(scanX, h);
    ctx.stroke();
  },

  updateSoulFields() {
    const container = document.getElementById('soul-fields');
    if (!container) return;
    const d = state.display;
    const hash = SOUL_DISPLAY.map(([k]) => k + ':' + (d[k] ?? '')).join('|');
    if (hash === this._lastFieldsHash) return;
    this._lastFieldsHash = hash;
    let html = '';
    for (const [key, label] of SOUL_DISPLAY) {
      const val = d[key];
      html += `<div class="soul-field"><span class="soul-key">${label}</span><span class="soul-val">${val !== undefined && val !== null ? val : '—'}</span></div>`;
    }
    container.innerHTML = html;
  },

  updateStatusBar() {
    const d = state.display;
    const running = state.target.engine_running;
    const dot = document.querySelector('.status-dot');
    const text = document.querySelector('.status-text');
    const modeEl = document.querySelector('.status-mode');
    const tickEl = document.querySelector('.status-tick');
    const pidEl = document.querySelector('.status-pid');
    if (dot) { dot.className = 'status-dot ' + (running ? 'on' : 'off'); }
    if (text) text.textContent = running ? 'ALIVE' : 'DORMANT';
    if (modeEl) modeEl.textContent = 'Mode: ' + (d.mode !== undefined ? d.mode : '—');
    if (tickEl) tickEl.textContent = 'Tick: ' + (d.tick || 0);
    if (pidEl) pidEl.textContent = 'PID: ' + (state.target.pid || '—');
  },
};

window.vitals = vitals;
