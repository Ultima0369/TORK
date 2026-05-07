// ── 事件总线 ── 封装 Tauri listen/emit

const state = window.state;
const pushEcg = window.pushEcg;

let listenFn = null;
let emitFn = null;

const eventBus = {
  async init() {
    try {
      const tauri = window.__TAURI__;
      console.log('[EB] __TAURI__:', !!tauri, tauri ? Object.keys(tauri) : 'N/A');
      if (tauri && tauri.event) {
        listenFn = tauri.event.listen;
        emitFn = tauri.event.emit;
        console.log('[EB] listen/emit bound');
      } else {
        console.warn('[EB] __TAURI__.event not available');
      }
    } catch (e) {
      console.warn('[EB] init error:', e);
    }
  },

  async listen(event, handler) {
    if (listenFn) {
      return listenFn(event, (e) => handler(e.payload));
    }
    return null;
  },

  async emit(event, payload) {
    if (emitFn) {
      return emitFn(event, payload);
    }
  },

  handleUpdate(payload) {
    console.log('[EB] update received, soul:', !!payload.soul, 'instincts:', !!payload.instincts);
    const t = state.target;
    const s = payload.soul;
    const inst = payload.instincts;

    t.engine_running = payload.engine_running;
    t.pid = payload.pid;

    // mentor/dispatch — 可选字段
    t.mentor = payload.mentor || null;
    t.dispatch = payload.dispatch || null;

    if (s) {
      state.hasData = true;
      t.tick = s.tick;
      t.drive = s.drive;
      t.hw_stress = s.hw_stress;
      t.mode = s.mode;
      t.tln_action = s.tln_action;
      t.tln_modify = s.tln_modify;
      t.tln_explore = s.tln_explore;
      t.tln_energy = s.tln_energy;
      t.gen_count = s.gen_count;
      t.learn_count = s.learn_count;
      t.experience_count = s.experience_count;
      t.mutation_count = s.mutation_count;
      t.best_score = s.best_score;
      t.sandbox_level = s.sandbox_level;
      t.cloud_connected = s.cloud_connected;
      t.branch_id = s.branch_id;
      t.self_pid = s.self_pid;
      t.ppid = s.ppid;
      t.code_insns = s.code_insns;
      t.code_ctrl = s.code_ctrl;
      t.fission_count = s.fission_count;
      t.wins = s.wins;
      t.agreed = s.agreed;
      t.node_id = s.node_id;
      t.consensus_vector = s.consensus_vector;

      if (s.tick !== state.lastTick) {
        pushEcg(eventBus.computeEcgValue(s));
        state.lastTick = s.tick;
      }
    }

    if (inst) {
      t.fear = inst.fear;
      t.desire = inst.desire;
      t.curiosity = inst.curiosity;
    }
  },

  computeEcgValue(soul) {
    // 防御性默认值，避免 undefined 导致 NaN
    const heartbeat = Math.sin((soul.tick || 0) * 0.1) * 20;
    const breath = (soul.drive || 0) * 0.5;
    const nerve = ((soul.tln_action || 0) + (soul.tln_modify || 0) + (soul.tln_explore || 0) + (soul.tln_energy || 0)) * 3;
    // 缩放 nerve 使 ECG 在合理范围内显示
    const nerveScaled = nerve / 10;
    return heartbeat + breath + nerveScaled + (Math.random() - 0.5) * 2;
  }
};
window.eventBus = eventBus;