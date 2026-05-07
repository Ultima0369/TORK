// ── 代码演化工作台 ── 文件树 + 编译/运行

const editor = {
  _cm: null,
  _currentFile: null,

  init() {
    const container = document.getElementById('code-editor');
    if (!container) return;
    // 使用 textarea 占位（contentEditable 有键盘冲突问题）
    const textarea = document.createElement('textarea');
    textarea.id = 'editor-textarea';
    textarea.className = 'editor-textarea';
    textarea.spellcheck = false;
    textarea.value = '// 按 E 打开编辑器\n// 选择左侧文件开始编辑';
    container.appendChild(textarea);

    // 绑定工具栏按钮
    const toolbar = document.getElementById('editor-toolbar');
    if (toolbar) {
      toolbar.addEventListener('click', (e) => {
        const action = e.target.dataset.action;
        if (!action) return;
        if (action === 'save') {
          this.saveFile();
        } else {
          this._execAction(action);
        }
      });
    }
    this.updateFileTree();
  },

  async loadFile(path) {
    // 路径安全：禁止 .. 和路径分隔符注入
    const sanitized = path.replace(/\.\./g, '').replace(/[\n\r]/g, '');
    if (window.__TAURI__) {
      try {
        const content = await window.__TAURI__.core.invoke('read_file', { path: sanitized });
        const textarea = document.getElementById('editor-textarea');
        if (textarea) textarea.value = content;
        this._currentFile = sanitized;
        this.updateFileTree();
      } catch (e) {
        console.warn('Failed to load file:', e);
      }
    }
  },

  async saveFile() {
    if (!this._currentFile) return;
    const textarea = document.getElementById('editor-textarea');
    if (!textarea) return;
    if (window.__TAURI__) {
      try {
        await window.__TAURI__.core.invoke('write_file', {
          path: this._currentFile,
          content: textarea.value
        });
      } catch (e) {
        console.warn('Failed to save file:', e);
      }
    }
  },

  async updateFileTree() {
    const tree = document.getElementById('file-tree');
    if (!tree) return;
    if (window.__TAURI__) {
      try {
        const entries = await window.__TAURI__.core.invoke('list_dir', { path: 'src' });
        tree.innerHTML = '';
        for (const e of entries) {
          const item = document.createElement('div');
          item.className = 'file-item';
          const safeName = e.name.replace(/[<>&"']/g, '');
          item.textContent = safeName;
          item.addEventListener('click', () => {
            this.loadFile('src/' + safeName);
          });
          tree.appendChild(item);
        }
      } catch (e) {
        tree.innerHTML = '';
        const err = document.createElement('div');
        err.className = 'file-item';
        err.textContent = '无法读取目录';
        tree.appendChild(err);
      }
    }
  },

  async _execAction(action) {
    if (!window.__TAURI__) return;
    try {
      switch (action) {
        case 'compile':
          await window.__TAURI__.core.invoke('torkd_query', { command: 'compile' });
          break;
        case 'run':
          await window.__TAURI__.core.invoke('torkd_query', { command: 'run' });
          break;
        case 'evolve':
          await window.__TAURI__.core.invoke('trigger_evolution', {});
          break;
      }
    } catch (e) {
      console.warn('Editor action failed:', e);
    }
  }
};window.editor = editor;
