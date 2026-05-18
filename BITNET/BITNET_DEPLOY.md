# BitNet b1.58 极简部署指南

**目标**: 在本地部署微软 BitNet b1.58 二值化大语言模型

---

## 第一步：克隆项目并安装依赖

```bash
git clone --recursive https://github.com/microsoft/BitNet.git
cd BitNet
pip install -r requirements.txt
```

---

## 第二步：构建项目并下载模型

下方命令会自动完成编译，并从 Hugging Face 下载微软官方的 2B 三进制模型：

```bash
python setup_env.py --hf-repo microsoft/BitNet-b1.58-2B-4T-gguf -q i2_s
```

**参数说明**:
- `--hf-repo`: Hugging Face 模型仓库名
- `-q i2_s`: 量化级别（i2_s 表示 2-bit 对称量化）

下载后的模型将保存到 `models/BitNet-b1.58-2B-4T/` 目录。

---

## 第三步：运行推理

```bash
python run_inference.py \
  -m models/BitNet-b1.58-2B-4T/ggml-model-i2_s.gguf \
  -p "你是一个 helpful assistant" \
  -cnv
```

**参数说明**:
- `-m`: 模型文件路径
- `-p`: 提示词 (prompt)
- `-cnv`: 对话模式 (conversational)

---

## 注意事项

1. **硬件要求**: BitNet b1.58 2B 模型需至少 4GB GPU 显存（CPU 模式需 8GB 内存）
2. **首次运行**: 第二步的模型下载可能需要 1-2 GB 磁盘空间
3. **加速推理**: 如已安装 CUDA，会自动使用 GPU 加速
4. **更多参数**: 运行 `python run_inference.py --help` 查看完整选项

---

**参考来源**: [microsoft/BitNet GitHub](https://github.com/microsoft/BitNet)
