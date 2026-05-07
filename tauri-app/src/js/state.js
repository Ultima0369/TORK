// ── 状态中心 ── target(数据源) + display(插值渲染) + 历史

const SMOOTH_FAST = 8;   // fear/desire/curiosity — 本能快速响应
const SMOOTH_MED  = 5;   // drive/hw_stress — 中速
const SMOOTH_SLOW = 3;   // tln_* — TLN 慢速平滑

const ECG_HIST_LEN = 300;

const state = {
  target: {
    engine_running: false,
    pid: null,
    mentor: null,
    dispatch: null,
    tick: 0,
    drive: 0,
    hw_stress: 0,
    mode: 0,
    tln_action: 0,
    tln_modify: 0,
    tln_explore: 0,
    tln_energy: 0,
    gen_count: 0,
    learn_count: 0,
    experience_count: 0,
    mutation_count: 0,
    best_score: 0,
    sandbox_level: 0,
    cloud_connected: 0,
    branch_id: 0,
    self_pid: 0,
    ppid: 0,
    code_insns: 0,
    code_ctrl: 0,
    fission_count: 0,
    wins: 0,
    agreed: 0,
    node_id: '',
    consensus_vector: '',
    fear: 0,
    desire: 0,
    curiosity: 0,
  },

  display: {
    tick: 0,
    drive: 0,
    hw_stress: 0,
    mode: 0,
    tln_action: 0,
    tln_modify: 0,
    tln_explore: 0,
    tln_energy: 0,
    gen_count: 0,
    learn_count: 0,
    experience_count: 0,
    mutation_count: 0,
    best_score: 0,
    sandbox_level: 0,
    cloud_connected: 0,
    branch_id: 0,
    fear: 0,
    desire: 0,
    curiosity: 0,
  },

  hasData: false,
  lastTick: -1,
  prevDisplayTick: -1,

  ecgHistory: new Float32Array(ECG_HIST_LEN),
  ecgHistoryLen: 0,

  interpolate(dt) {
    const t = this.target;
    const d = this.display;

    d.tick = t.tick;
    d.gen_count = t.gen_count;
    d.mode = t.mode;
    d.sandbox_level = t.sandbox_level;
    d.cloud_connected = t.cloud_connected;
    d.branch_id = t.branch_id;
    d.learn_count = t.learn_count;
    d.experience_count = t.experience_count;
    d.mutation_count = t.mutation_count;
    d.best_score = t.best_score;

    lerp(d, t, dt, SMOOTH_FAST, ['fear', 'desire', 'curiosity']);
    lerp(d, t, dt, SMOOTH_MED, ['drive', 'hw_stress']);
    lerp(d, t, dt, SMOOTH_SLOW, ['tln_action', 'tln_modify', 'tln_explore', 'tln_energy']);
    d.tln_action = Math.max(-128, Math.min(127, d.tln_action));
    d.tln_modify = Math.max(-128, Math.min(127, d.tln_modify));
    d.tln_explore = Math.max(-128, Math.min(127, d.tln_explore));
    d.tln_energy = Math.max(-128, Math.min(127, d.tln_energy));
  },
};

function lerp(display, target, dt, speed, keys) {
  const factor = 1 - Math.exp(-dt * speed);
  for (const k of keys) {
    display[k] += (target[k] - display[k]) * factor;
  }
}

function pushEcg(val) {
  const h = state.ecgHistory;
  const len = state.ecgHistoryLen;
  if (len < ECG_HIST_LEN) {
    h[len] = val;
    state.ecgHistoryLen++;
  } else {
    h.copyWithin(0, 1);
    h[ECG_HIST_LEN - 1] = val;
  }
}
window.state = state;
window.interpolate = interpolate;
window.pushEcg = pushEcg;
