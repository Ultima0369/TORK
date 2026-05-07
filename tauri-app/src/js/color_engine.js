// ── 颜色引擎 ── 连续 HSL 插值，本能驱动色相

// 本能 → 色相映射
// fear → 红(0-30°), desire → 琥珀(30-60°), curiosity → 青(180-220°)
const HUE_MAP = {
  fear: [0, 30],
  desire: [30, 60],
  curiosity: [180, 220],
};

// 缓存：避免每帧 getComputedStyle
let _cachedHSL = { h: 190, s: 30, l: 45 };

function computeAccentColor(instincts) {
  const { fear, desire, curiosity } = instincts;
  const total = fear + desire + curiosity;

  // 全零时返回中性青色（避免闪红）
  if (total < 0.01) {
    return { h: 190, s: 30, l: 45 };
  }

  // 加权混合色相
  let hue = 0;
  hue += (fear / total) * mix(HUE_MAP.fear[0], HUE_MAP.fear[1], fear / 100);
  hue += (desire / total) * mix(HUE_MAP.desire[0], HUE_MAP.desire[1], desire / 100);
  hue += (curiosity / total) * mix(HUE_MAP.curiosity[0], HUE_MAP.curiosity[1], curiosity / 100);

  // 饱和度 = 最大本能强度
  const maxInstinct = Math.max(fear, desire, curiosity);
  const sat = 30 + (maxInstinct / 100) * 55; // 30-85%

  // 亮度 = 中等偏暗
  const light = 45 + (maxInstinct / 100) * 15; // 45-60%

  return { h: hue % 360, s: sat, l: light };
}

function mix(a, b, t) {
  return a + (b - a) * Math.min(1, Math.max(0, t));
}

// 更新 CSS 变量 + 缓存
function applyColor(h, s, l) {
  _cachedHSL = { h, s, l };
  const root = document.documentElement.style;
  root.setProperty('--accent-h', h.toFixed(1));
  root.setProperty('--accent-s', s.toFixed(1) + '%');
  root.setProperty('--accent-l', l.toFixed(1) + '%');
}

// 获取当前 accent 颜色（直接从缓存，不读 DOM）
function accentHSL() {
  return _cachedHSL;
}

window.colorEngine = { computeAccentColor, applyColor, accentHSL };
window.accentHSL = accentHSL;
