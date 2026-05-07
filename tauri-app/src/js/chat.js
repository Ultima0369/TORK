// ── 智能对话 ── 情绪感知 + 云脑集成

const chat = {
  _sending: false,
  _maxMessages: 100,

  init() {
    const input = document.getElementById('chat-input');
    if (!input) return;
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        chatSend();
      }
    });
  },

  addMessage(text, sender, mood) {
    const container = document.getElementById('chat-messages');
    if (!container) return;
    const msg = document.createElement('div');
    msg.className = `chat-msg ${sender}`;
    msg.textContent = text;
    if (mood) {
      const moodEl = document.createElement('div');
      moodEl.className = 'msg-mood';
      moodEl.textContent = mood;
      msg.appendChild(moodEl);
    }
    container.appendChild(msg);
    container.scrollTop = container.scrollHeight;

    // 限制消息数量
    while (container.children.length > this._maxMessages) {
      container.removeChild(container.firstChild);
    }
  }
};

async function chatSend() {
  if (chat._sending) return;
  const input = document.getElementById('chat-input');
  if (!input || !input.value.trim()) return;

  const text = input.value.trim();
  input.value = '';
  chat._sending = true;
  input.disabled = true;

  chat.addMessage(text, 'user');

  if (!window.__TAURI__) {
    chat.addMessage('TORK 引擎未连接', 'tork');
    chat._sending = false;
    input.disabled = false;
    return;
  }

  try {
    const mood = document.getElementById('mood-label')?.textContent || '平静';
    const response = await window.__TAURI__.core.invoke('chat_send', {
      message: text,
      mood: mood
    });
    chat.addMessage(response || '(沉默)', 'tork', mood);
  } catch (e) {
    chat.addMessage('云脑连接失败', 'tork');
  } finally {
    chat._sending = false;
    input.disabled = false;
  }
}window.chat = chat;
