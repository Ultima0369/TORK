#!/bin/bash
# ── TORK 端到端验证 ──────────────────────────────────────────
# 启动引擎 → 发命令 → 验证返回 → PASS/FAIL
# 用法: bash tools/tork_e2e.sh
set -e

SOCKET="/tmp/torkd.sock"
ENGINE="./build/tork_engine"
ENGINE_PID=""
PASS=0
FAIL=0

cleanup() {
    [ -n "$ENGINE_PID" ] && kill "$ENGINE_PID" 2>/dev/null
    pkill -f "tork_core" 2>/dev/null
    rm -f "$SOCKET"
}
trap cleanup EXIT

# ── Helper: send command to torkd socket ──
tork_send() {
    local cmd="$1"
    local timeout="${2:-3}"
    # Small C helper compiled once
    if [ ! -x /tmp/tork_e2e_client ]; then
        cat > /tmp/tork_e2e_client.c << 'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }
    char send_buf[4096];
    snprintf(send_buf, sizeof(send_buf), "%s\n", argv[2]);
    write(fd, send_buf, strlen(send_buf));
    struct timeval tv = { .tv_sec = atoi(argc > 3 ? argv[3] : "5"), .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char recv_buf[16384];
    memset(recv_buf, 0, sizeof(recv_buf));
    ssize_t total = 0, n;
    while ((n = read(fd, recv_buf + total, sizeof(recv_buf) - total - 1)) > 0) total += n;
    if (total > 0) { recv_buf[total] = '\0'; printf("%s", recv_buf); }
    close(fd);
    return 0;
}
CEOF
        gcc -o /tmp/tork_e2e_client /tmp/tork_e2e_client.c 2>/dev/null
    fi
    /tmp/tork_e2e_client "$SOCKET" "$cmd" "$timeout"
}

# ── Helper: check result ──
check() {
    local name="$1"
    local result="$2"
    local expect="$3"
    if echo "$result" | grep -q "$expect"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name (expected '$expect', got: $(echo "$result" | head -1))"
        FAIL=$((FAIL + 1))
    fi
}

# ── Start engine ──
echo "═══ TORK E2E Verification ═══"
echo "Starting engine..."
rm -f "$SOCKET"
$ENGINE 500 &>/tmp/tork_e2e.log &
ENGINE_PID=$!
sleep 4

if [ ! -S "$SOCKET" ]; then
    echo "FAIL: socket not created after 4s"
    exit 1
fi
echo "Socket ready."

# ── Tests ──

echo ""
echo "── Basic ──"
R=$(tork_send "ping")
check "ping" "$R" "pong"

R=$(tork_send "status")
check "status" "$R" "tick"

echo ""
echo "── Exec ──"
R=$(tork_send "exec:echo hello_e2e")
check "exec echo" "$R" "hello_e2e"

R=$(tork_send "exec:wc -l Makefile" 5)
check "exec wc" "$R" "exit_code"

echo ""
echo "── Audit ──"
R=$(tork_send "audit:benchmark/memcpy/ref.s:memcpy_tork" 5)
check "audit ref.s" "$R" "risk_score"

echo ""
echo "── Task Queue ──"
R=$(tork_send "task:exec:echo task_test" 3)
check "task submit" "$R" "task_id"

sleep 2
R=$(tork_send "result:1" 3)
check "result query" "$R" "status"

R=$(tork_send "tasks" 3)
check "tasks overview" "$R" "completed"

echo ""
echo "── Codegen ──"
R=$(tork_send "codegen:compile:memcpy_byte_loop" 10)
check "codegen compile" "$R" "compile_ok"

echo ""
echo "════════════════════════════════════════"
echo "Results: $PASS passed, $FAIL failed"
echo "════════════════════════════════════════"

[ "$FAIL" -eq 0 ] && exit 0 || exit 1