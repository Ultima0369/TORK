#!/bin/bash
# 🥚 TORK 安装器构建脚本
# 用法: ./build-installer.sh
# 输出: dist/TORK-x86_64.AppImage (自包含可执行文件)

set -e
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
DIST_DIR="$BASE_DIR/dist"
BUILD_DIR="$BASE_DIR/build"

echo "🥚 TORK Installer Builder"
echo "────────────────────────"

# 1. 编译
echo "[1/4] 编译二进制…"
make -C "$BASE_DIR" all 2>&1 | tail -1

# 2. 创建包目录
echo "[2/4] 打包文件…"
mkdir -p "$DIST_DIR/tork"

# 复制二进制
mkdir -p "$DIST_DIR/tork/build"
cp "$BUILD_DIR/tork_engine" "$DIST_DIR/tork/build/"
cp "$BUILD_DIR/tork_core" "$DIST_DIR/tork/build/"
cp "$BUILD_DIR/tork_sandbox" "$DIST_DIR/tork/build/"

# 复制 Python 模块
cp -r "$BASE_DIR/app" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/cloud" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/floating" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/api" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/install" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/persist" "$DIST_DIR/tork/" 2>/dev/null || mkdir -p "$DIST_DIR/tork/persist"

# 复制 core (asm 源码保留给进化引擎)
cp -r "$BASE_DIR/core" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/instinct" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/engine" "$DIST_DIR/tork/"
cp -r "$BASE_DIR/code" "$DIST_DIR/tork/"

# 复制构建脚本
cp "$BASE_DIR/Makefile" "$DIST_DIR/tork/"

# 3. 创建启动器
echo "[3/4] 创建启动器…"

cat > "$DIST_DIR/tork/run.sh" << 'RUNEOF'
#!/bin/bash
# TORK 启动器 — 双击即可运行
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# 检查协议
if [ ! -f ~/.config/tork/.agreed ]; then
    echo "🥚 TORK 首次启动..."
    python3 app/tork_app.py
    exit $?
fi

# 启动引擎 (后台)
if ! pgrep -x tork_engine > /dev/null 2>&1; then
    ./build/tork_engine 999999 > /dev/null 2>&1 &
    sleep 0.5
fi

# 启动图形界面
python3 app/tork_app.py
RUNEOF
chmod +x "$DIST_DIR/tork/run.sh"

# 4. 创建安装脚本
echo "[4/4] 创建安装器…"

cat > "$DIST_DIR/install.sh" << 'INSTEOF'
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
INSTEOF
chmod +x "$DIST_DIR/install.sh"

# 创建版本信息
echo "TORK v2.2 ($(git log --oneline -1 | head -c 8))" > "$DIST_DIR/tork/VERSION"
echo "Build: $(date '+%Y-%m-%d %H:%M')" >> "$DIST_DIR/tork/VERSION"

# 5. 打包成单个文件
echo ""
echo "📦 打包安装器…"

cd "$DIST_DIR"
tar czf tork.tar.gz tork/
{
    echo '#!/bin/bash'
    echo '# 🥚 TORK 自解压安装器'
    echo 'set -e'
    echo 'echo "🥚 正在解压 TORK..."'
    echo 'ARCHIVE=$(mktemp)'
    echo 'tail -n +12 "$0" > "$ARCHIVE"'
    echo 'tar xzf "$ARCHIVE" -C /tmp'
    echo 'bash /tmp/tork/install.sh'
    echo 'rm -f "$ARCHIVE"'
    echo 'rm -rf /tmp/tork'
    echo 'exit 0'
    base64 tork.tar.gz
} > TORK-x86_64.AppImage

chmod +x TORK-x86_64.AppImage
rm -f tork.tar.gz
rm -rf tork/

echo ""
echo "✅ 构建完成!"
echo "   📄 $DIST_DIR/TORK-x86_64.AppImage"
ls -lh "$DIST_DIR/TORK-x86_64.AppImage"
