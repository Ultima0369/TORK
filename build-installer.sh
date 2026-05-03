#!/bin/bash
# 🥚 TORK 安装器构建脚本
# 输出: dist/TORK-x86_64.AppImage

set -e
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
DIST_DIR="$BASE_DIR/dist"

echo "🥚 TORK Installer Builder"
echo "────────────────────────"

# 1. 编译
echo "[1/4] 编译二进制…"
make -C "$BASE_DIR" all 2>&1 | tail -1

# 2. 打包为单一目录
echo "[2/4] 打包文件…"
PKG_DIR="$DIST_DIR/tork_pkg"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"/{build,app,cloud,floating,api,install,persist}

# 二进制
cp "$BASE_DIR/build/tork_engine" "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_core" "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_sandbox" "$PKG_DIR/build/"

# Python 模块
cp -r "$BASE_DIR/app/tork_app.py" "$PKG_DIR/app/"
cp "$BASE_DIR/cloud/cloud_protocol.py" "$PKG_DIR/cloud/"
cp "$BASE_DIR/cloud/evolution.py" "$PKG_DIR/cloud/"
cp "$BASE_DIR/api/tork_api.py" "$PKG_DIR/api/"
cp -r "$BASE_DIR/install/agreement.h" "$PKG_DIR/install/" 2>/dev/null || true

# 持久化目录
cp -r "$BASE_DIR/persist"/* "$PKG_DIR/persist/" 2>/dev/null || true

# 版本
echo "TORK v2.2 ($(cd "$BASE_DIR" && git log --oneline -1 | head -c 8))" > "$PKG_DIR/VERSION"
echo "Build: $(date '+%Y-%m-%d %H:%M')" >> "$PKG_DIR/VERSION"

# 3. 创建安装器
echo "[3/4] 创建安装器…"

cat > "$PKG_DIR/install.sh" << 'INSTEOF'
#!/bin/bash
# 🥚 TORK 用户安装器
set -e

INSTALL_DIR="$HOME/.local/share/tork"
BIN_DIR="$HOME/.local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"
CONFIG_DIR="$HOME/.config/tork"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$INSTALL_DIR" "$BIN_DIR" "$DESKTOP_DIR" "$CONFIG_DIR"

# 复制所有文件
cp -r "$SCRIPT_DIR"/* "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/build/"*
chmod +x "$INSTALL_DIR/run.sh"

# 创建命令行链接
ln -sf "$INSTALL_DIR/run.sh" "$BIN_DIR/tork"

# 添加入 PATH (如果未添加)
if ! grep -q '.local/bin' "$HOME/.bashrc" 2>/dev/null; then
    echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
fi

# 创建桌面快捷方式
cat > "$DESKTOP_DIR/tork.desktop" << DESKEOF
[Desktop Entry]
Name=TORK
Comment=TORK — 数字存在体
Exec=$INSTALL_DIR/run.sh
Icon=$INSTALL_DIR/icon.png
Type=Application
Terminal=false
Categories=Utility;
StartupNotify=true
DESKEOF

# 快捷方式可执行
chmod +x "$DESKTOP_DIR/tork.desktop"

echo "✅ TORK 已安装到 $INSTALL_DIR"
echo "   命令行: tork"
echo "   桌面:   TORK 快捷方式"
echo ""
INSTEOF
chmod +x "$PKG_DIR/install.sh"

# 创建运行脚本
cat > "$PKG_DIR/run.sh" << 'RUNEOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# 如果没有协议 → 启动图形界面（协议内置在 tork_app.py 中）
python3 app/tork_app.py
RUNEOF
chmod +x "$PKG_DIR/run.sh"

# 4. 打包为自解压单文件
echo "[4/4] 构建 AppImage…"

# 打包为 tar.gz
cd "$DIST_DIR"
tar czf tork_pkg.tar.gz -C "$DIST_DIR" tork_pkg/

# 创建自解压脚本
{
    echo '#!/bin/bash'
    echo '# 🥚 TORK-x86_64.AppImage — 自解压安装器'
    echo 'set -e'
    echo 'INSTALL_DIR="$HOME/.local/share/tork"'
    echo 'echo "🥚 安装 TORK..."'
    echo 'mkdir -p "$INSTALL_DIR"'
    echo 'ARCHIVE=$(mktemp)'
    echo 'tail -n +20 "$0" > "$ARCHIVE"'
    echo 'tar xzf "$ARCHIVE" -C "$INSTALL_DIR" 2>/dev/null || ('
    echo '    # 如果目标已存在，先清理'
    echo '    rm -rf "$INSTALL_DIR"'
    echo '    mkdir -p "$INSTALL_DIR"'
    echo '    tar xzf "$ARCHIVE" -C "$INSTALL_DIR"'
    echo ')'
    echo 'rm -f "$ARCHIVE"'
    echo 'chmod +x "$INSTALL_DIR/tork_pkg/build/"*'
    echo 'chmod +x "$INSTALL_DIR/tork_pkg/run.sh"'
    echo 'echo "✅ TORK 就绪"'
    echo 'exec "$INSTALL_DIR/tork_pkg/run.sh"'
    base64 tork_pkg.tar.gz
} > TORK-x86_64.AppImage

chmod +x TORK-x86_64.AppImage
rm -rf tork_pkg tork_pkg.tar.gz

echo ""
echo "✅ 构建完成!"
echo "   📄 $DIST_DIR/TORK-x86_64.AppImage"
ls -lh "$DIST_DIR/TORK-x86_64.AppImage"
