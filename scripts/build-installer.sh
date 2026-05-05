#!/bin/bash
# TORK 安装器构建脚本 v3.1
# 输出: dist/TORK-x86_64.AppImage — 真正的 ELF 二进制，双击即可运行
# 包含: 核心引擎 + Web UI + 本能系统 + 学习回路 + 云端协议 + 讯飞星辰 API

set -e
BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$BASE_DIR/dist"

echo "TORK v3.1 构建器"
echo "────────────────────────"

# ── 1. 编译全部 ──
echo "[1/5] 编译全部组件…"
make -C "$BASE_DIR" all 2>&1 | grep -E "error|warning|built|完成" | head -5
make -C "$BASE_DIR" grid 2>&1 | tail -1

# ── 2. 打包数据 ──
echo "[2/5] 打包运行时数据…"
PKG_DIR="/tmp/tork_pkg_elf_v31"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"/{build,app,cloud,api,floating,grid,learning,persist,web/static,shared}

# 二进制
cp "$BASE_DIR/build/tork_engine"   "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_core"     "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_sandbox"  "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_grid"     "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_ask"      "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork"          "$PKG_DIR/build/"
cp "$BASE_DIR/build/torkd_start"   "$PKG_DIR/build/"

# Python 应用层
cp "$BASE_DIR/app/tork_app.py"          "$PKG_DIR/app/"
cp "$BASE_DIR/cloud/cloud_protocol.py"  "$PKG_DIR/cloud/"
cp "$BASE_DIR/cloud/evolution.py"        "$PKG_DIR/cloud/"
cp "$BASE_DIR/cloud/evolution_daemon.py" "$PKG_DIR/cloud/"
cp "$BASE_DIR/api/tork_api.py"          "$PKG_DIR/api/"
cp "$BASE_DIR/api/api_config.json"      "$PKG_DIR/api/"
cp "$BASE_DIR/floating/tork_dashboard.py" "$PKG_DIR/floating/" 2>/dev/null || true
cp "$BASE_DIR/floating/tork_daemon.py"    "$PKG_DIR/floating/" 2>/dev/null || true

# Web UI
cp "$BASE_DIR/web/tork_web.py"         "$PKG_DIR/web/"
cp "$BASE_DIR/web/torkd_bridge.py"      "$PKG_DIR/web/"
cp "$BASE_DIR/web/static/dashboard.html" "$PKG_DIR/web/static/"

# 共享模块
cp "$BASE_DIR/shared/soul_parser.py"    "$PKG_DIR/shared/"

# 启动脚本
cp "$BASE_DIR/scripts/tork.sh"              "$PKG_DIR/"
chmod +x "$PKG_DIR/tork.sh"

# Grid 源代码 (运行时供 Python 调用)
cp "$BASE_DIR/src/grid/tork_grid.c"  "$PKG_DIR/grid/"
cp "$BASE_DIR/src/grid/tork_grid.h"  "$PKG_DIR/grid/"
cp "$BASE_DIR/src/grid/grid_main.c"  "$PKG_DIR/grid/"

# 学习模块头文件 (供 runtime 引用)
mkdir -p "$PKG_DIR/learning"
cp "$BASE_DIR/src/learning/"*.h "$PKG_DIR/learning/" 2>/dev/null || true

# 持久化目录 (空, 运行时生成)
touch "$PKG_DIR/persist/.keep"

# 版本信息
GIT_HASH=$(cd "$BASE_DIR" && git log --oneline -1 | head -c 8)
GIT_MSG=$(cd "$BASE_DIR" && git log --oneline -1 | cut -d' ' -f2-)
cat > "$PKG_DIR/VERSION" << VEOF
TORK v3.1
Commit: $GIT_HASH
Message: $GIT_MSG
Build: $(date '+%Y-%m-%d %H:%M')
Files: $(find "$BASE_DIR" -name "*.c" -o -name "*.h" -o -name "*.asm" -o -name "*.py" | grep -v /.git | wc -l) source files
Lines: $(cat $(find "$BASE_DIR" -name "*.c" -o -name "*.h" -o -name "*.asm" -o -name "*.py" | grep -v /.git) 2>/dev/null | wc -l) lines of code
Capabilities: core+instinct+learning+MCTS+branch+pattern+replay+snapshot+energy+grid+watcher+ask+web+chat
API: 讯飞星辰 MaaS (astron-code-latest)
VEOF

# 打包
cd "$PKG_DIR"
tar czf /tmp/tork_embedded_v31.tar.gz .
cd "$BASE_DIR"

echo "   📦 打包完成: $(wc -c < /tmp/tork_embedded_v31.tar.gz) bytes"

# ── 3. 生成嵌入头文件 ──
echo "[3/5] 生成嵌入数据头…"
python3 << 'GEN'
import os
data = open("/tmp/tork_embedded_v31.tar.gz", "rb").read()
with open("/tmp/embedded_data_v31.h", "w") as f:
    f.write("#ifndef EMBEDDED_DATA_V31_H\n#define EMBEDDED_DATA_V31_H\n\n")
    f.write(f"#define EMBEDDED_SIZE {len(data)}\n\n")
    f.write("static unsigned char embedded_data[EMBEDDED_SIZE] = {\n")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        f.write(f"    {hex_bytes},\n")
    f.write("};\n\n#endif\n")
print(f"   📝 头文件: {len(data)} bytes")
GEN

# ── 4. 编译 C 启动器 ──
echo "[4/5] 编译 AppImage 启动器…"
mkdir -p "$DIST_DIR"
cp /tmp/embedded_data_v31.h "$DIST_DIR/embedded_data.h"

cat > "$DIST_DIR/tork_launcher.c" << 'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "embedded_data.h"

static void ensure_dir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) mkdir(path, 0755);
}

static void ensure_dirs(const char *base) {
    char buf[4096];
    const char *subs[] = {"build","app","cloud","api","floating","grid",
                          "learning","persist","web/static","shared","",NULL};
    for (int i = 0; subs[i]; i++) {
        snprintf(buf, sizeof(buf), "%s/%s", base, subs[i]);
        ensure_dir(buf);
    }
}

static int extract(const unsigned char *data, size_t size, const char *dest) {
    char tmpfile[] = "/tmp/tork_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(fd, data + written, size - written);
        if (n < 0) { close(fd); unlink(tmpfile); return -1; }
        written += n;
    }
    close(fd);
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "tar xzf %s -C %s 2>/dev/null", tmpfile, dest);
    int ret = system(cmd);
    unlink(tmpfile);
    return ret;
}

int main(int argc, char **argv) {
    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "No HOME\n"); return 1; }

    char install_dir[4096];
    snprintf(install_dir, sizeof(install_dir), "%s/.local/share/tork", home);

    ensure_dirs(install_dir);

    /* Extract embedded tarball */
    if (extract(embedded_data, EMBEDDED_SIZE, install_dir) != 0) {
        char cmd[8192];
        snprintf(cmd, sizeof(cmd),
            "rm -rf %s/* 2>/dev/null; mkdir -p %s/build %s/app %s/cloud %s/api "
            "%s/floating %s/grid %s/learning %s/persist %s/web/static %s/shared",
            install_dir, install_dir, install_dir, install_dir, install_dir,
            install_dir, install_dir, install_dir, install_dir, install_dir, install_dir);
        system(cmd);
        extract(embedded_data, EMBEDDED_SIZE, install_dir);
    }

    /* Ensure binaries are executable */
    char perm_cmd[4096];
    snprintf(perm_cmd, sizeof(perm_cmd), "chmod +x %s/build/* %s/tork.sh 2>/dev/null",
             install_dir, install_dir);
    system(perm_cmd);

    /* Read version */
    char ver_path[4096];
    snprintf(ver_path, sizeof(ver_path), "%s/VERSION", install_dir);
    FILE *vf = fopen(ver_path, "r");
    if (vf) {
        char buf[128];
        while (fgets(buf, sizeof(buf), vf)) printf("%s", buf);
        fclose(vf);
        printf("\n");
    }

    /* Launch Web Dashboard (preferred) via tork.sh */
    char tork_sh[4096];
    snprintf(tork_sh, sizeof(tork_sh), "%s/tork.sh", install_dir);
    struct stat st = {0};
    if (stat(tork_sh, &st) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            chdir(install_dir);
            execlp("bash", "bash", tork_sh, "dashboard", NULL);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
        return 0;
    }

    /* Fallback: Tkinter app */
    char app_script[4096];
    snprintf(app_script, sizeof(app_script), "%s/app/tork_app.py", install_dir);
    if (stat(app_script, &st) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            chdir(install_dir);
            execlp("python3", "python3", app_script, NULL);
            execlp("python", "python", app_script, NULL);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return 0;
}
CEOF

cd "$DIST_DIR"
gcc -O2 -o TORK-x86_64.AppImage tork_launcher.c -s 2>&1
rm -f tork_launcher.c embedded_data.h
cd "$BASE_DIR"

# ── 5. 清理 ──
echo "[5/5] 清理临时文件…"
rm -rf /tmp/tork_pkg_elf_v31 /tmp/tork_embedded_v31.tar.gz /tmp/embedded_data_v31.h

echo ""
echo "✅ 构建完成!"
echo "   📄 $DIST_DIR/TORK-x86_64.AppImage"
ls -lh "$DIST_DIR/TORK-x86_64.AppImage"
file "$DIST_DIR/TORK-x86_64.AppImage"