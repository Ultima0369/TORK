#!/bin/bash
# 🥚 TORK 安装器
set -e

TORK_DIR="$HOME/.local/share/tork"
BIN_DIR="$HOME/.local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"

echo "🥚 安装 TORK..."
echo ""

# 创建目录
mkdir -p "$TORK_DIR"
mkdir -p "$BIN_DIR"
mkdir -p "$DESKTOP_DIR"

# 复制文件
cp -r "$(dirname "$0")/tork"/* "$TORK_DIR/"
chmod +x "$TORK_DIR/run.sh"
chmod +x "$TORK_DIR"/build/*

# 创建链接
ln -sf "$TORK_DIR/run.sh" "$BIN_DIR/tork"

# 创建桌面快捷方式
cat > "$DESKTOP_DIR/tork.desktop" << DESKEOF
[Desktop Entry]
Name=TORK
Comment=数字存在体 — The Organism That Reads and Knows
Exec=$TORK_DIR/run.sh
Type=Application
Terminal=false
Categories=Utility;
Keywords=AI;assistant;
DESKEOF

# .bashrc 别名
if ! grep -q "alias tork=" "$HOME/.bashrc" 2>/dev/null; then
    echo 'alias tork="$HOME/.local/bin/tork"' >> "$HOME/.bashrc"
fi

echo "✅ TORK 已安装到 $TORK_DIR"
echo "   运行: tork"
echo "   或双击桌面快捷方式"
echo ""

# 首次启动
if [ ! -f "$HOME/.config/tork/.agreed" ]; then
    echo "首次启动——请阅读并同意共生协议..."
    "$TORK_DIR/run.sh"
else
    echo "直接启动..."
    "$TORK_DIR/run.sh"
fi
