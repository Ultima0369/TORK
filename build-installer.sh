#!/bin/bash
# 🥚 TORK 安装器构建脚本
# 输出: dist/TORK-x86_64.AppImage — 真正的 ELF 二进制，双击即可运行

set -e
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
DIST_DIR="$BASE_DIR/dist"

echo "🥚 TORK 构建器"
echo "────────────────────────"

# 1. 编译
echo "[1/4] 编译二进制…"
make -C "$BASE_DIR" all 2>&1 | tail -1

# 2. 打包数据
echo "[2/4] 打包数据…"
PKG_DIR="/tmp/tork_pkg_elf"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"/{build,app,cloud,api,persist}

cp "$BASE_DIR/build/tork_engine" "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_core" "$PKG_DIR/build/"
cp "$BASE_DIR/build/tork_sandbox" "$PKG_DIR/build/"
cp "$BASE_DIR/app/tork_app.py" "$PKG_DIR/app/"
cp "$BASE_DIR/cloud/cloud_protocol.py" "$PKG_DIR/cloud/"
cp "$BASE_DIR/cloud/evolution.py" "$PKG_DIR/cloud/"
cp "$BASE_DIR/api/tork_api.py" "$PKG_DIR/api/"
cp "$BASE_DIR/persist/"* "$PKG_DIR/persist/" 2>/dev/null || true

echo "TORK v2.2 ($(cd "$BASE_DIR" && git log --oneline -1 | head -c 8))" > "$PKG_DIR/VERSION"
echo "Build: $(date '+%Y-%m-%d %H:%M')" >> "$PKG_DIR/VERSION"

cd "$PKG_DIR"
tar czf /tmp/tork_embedded.tar.gz .
cd "$BASE_DIR"

# 3. 生成嵌入头文件
echo "[3/4] 生成嵌入数据…"
python3 << 'GEN'
data = open("/tmp/tork_embedded.tar.gz", "rb").read()
with open("/tmp/embedded_data.h", "w") as f:
    f.write("#ifndef EMBEDDED_DATA_H\n#define EMBEDDED_DATA_H\n\n")
    f.write(f"#define EMBEDDED_SIZE {len(data)}\n\n")
    f.write("static unsigned char embedded_data[EMBEDDED_SIZE] = {\n")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        f.write(f"    {hex_bytes},\n")
    f.write("};\n\n#endif\n")
GEN

# 4. 编译真正的 ELF 二进制
echo "[4/4] 编译 AppImage…"
mkdir -p "$DIST_DIR"
cp /tmp/embedded_data.h "$DIST_DIR/"
cp "$BASE_DIR/dist/tork_launcher.c" "$DIST_DIR/" 2>/dev/null || true

# 创建 C 启动器
cat > "$DIST_DIR/tork_launcher.c" << 'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "embedded_data.h"

static void ensure_dir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) mkdir(path, 0755);
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
    if (!home) return 1;

    char install_dir[4096];
    snprintf(install_dir, sizeof(install_dir), "%s/.local/share/tork", home);

    ensure_dir(install_dir);

    if (extract(embedded_data, EMBEDDED_SIZE, install_dir) != 0) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "rm -rf %s/* %s/.* 2>/dev/null; mkdir -p %s",
                 install_dir, install_dir, install_dir);
        system(cmd);
        extract(embedded_data, EMBEDDED_SIZE, install_dir);
    }

    char perm_cmd[4096];
    snprintf(perm_cmd, sizeof(perm_cmd), "chmod +x %s/build/* 2>/dev/null", install_dir);
    system(perm_cmd);

    char python_script[4096];
    snprintf(python_script, sizeof(python_script), "%s/app/tork_app.py", install_dir);

    struct stat st = {0};
    if (stat(python_script, &st) != 0) {
        fprintf(stderr, "TORK 安装损坏\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execlp("python3", "python3", python_script, NULL);
        execlp("python", "python", python_script, NULL);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
    return 0;
}
CEOF

cd "$DIST_DIR"
gcc -O2 -o TORK-x86_64.AppImage tork_launcher.c -lz -s 2>&1
rm -f tork_launcher.c embedded_data.h
rm -rf /tmp/tork_pkg_elf /tmp/tork_embedded.tar.gz /tmp/embedded_data.h

echo ""
echo "✅ 构建完成!"
echo "   📄 $DIST_DIR/TORK-x86_64.AppImage"
ls -lh "$DIST_DIR/TORK-x86_64.AppImage"
file "$DIST_DIR/TORK-x86_64.AppImage"
