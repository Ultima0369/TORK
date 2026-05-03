#!/bin/bash
# ──────────────────────────────────────────────────────
# 🥚 TORK 启动脚本
# 用法: ./tork.sh [command]
# ──────────────────────────────────────────────────────

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON="$BASE_DIR/floating/tork_daemon.py"
DASHBOARD="$BASE_DIR/floating/tork_dashboard.py"
ENGINE="$BASE_DIR/build/tork_engine"
PROTOCOL="$BASE_DIR/cloud/cloud_protocol.py"
EVOLUTION="$BASE_DIR/cloud/evolution.py"

GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
DIM='\033[2m'
NC='\033[0m'

case "${1:-help}" in
    start)
        echo -e "${GREEN}🥚 TORK 启动中...${NC}"
        if [ ! -f "$ENGINE" ]; then
            echo -e "${YELLOW}⚠️  引擎未编译，先编译...${NC}"
            make -C "$BASE_DIR" all
        fi
        python3 "$DAEMON" all
        ;;

    dashboard)
        echo -e "${GREEN}🥚 启动仪表盘...${NC}"
        python3 "$DASHBOARD"
        ;;

    engine)
        echo -e "${GREEN}🥚 启动引擎...${NC}"
        if [ ! -f "$ENGINE" ]; then
            echo -e "${YELLOW}⚠️  引擎未编译，先编译...${NC}"
            make -C "$BASE_DIR" all
        fi
        "$ENGINE"
        ;;

    daemon)
        echo -e "${GREEN}🥚 启动守护进程 (后台)...${NC}"
        python3 "$DAEMON" all &
        echo -e "   PID: $!"
        ;;

    status)
        echo -e "${CYAN}📡 TORK 状态${NC}"
        echo -e "${DIM}────────────────────────${NC}"
        # 引擎
        if pgrep -x tork_core > /dev/null 2>&1; then
            echo -e "  引擎:     ${GREEN}✅ 运行中${NC} (PID: $(pgrep -x tork_core))"
        elif pgrep -x tork_engine > /dev/null 2>&1; then
            echo -e "  引擎:     ${GREEN}✅ 运行中${NC} (PID: $(pgrep -x tork_engine))"
        else
            echo -e "  引擎:     ${RED}⏹️  已停止${NC}"
        fi
        # 仪表盘
        if pgrep -f tork_dashboard.py > /dev/null 2>&1; then
            echo -e "  仪表盘:   ${GREEN}✅ 运行中${NC}"
        else
            echo -e "  仪表盘:   ${DIM}⏹️  未启动${NC}"
        fi
        # 守护进程
        if [ -f "$BASE_DIR/persist/daemon.pid" ]; then
            DPID=$(cat "$BASE_DIR/persist/daemon.pid")
            if kill -0 "$DPID" 2>/dev/null; then
                echo -e "  守护进程: ${GREEN}✅ 运行中${NC} (PID: $DPID)"
            else
                echo -e "  守护进程: ${YELLOW}⚠️  PID 文件残留${NC}"
            fi
        else
            echo -e "  守护进程: ${DIM}⏹️  未启动${NC}"
        fi
        # 云端
        if python3 -c "from tork_api import TorkAPI; a=TorkAPI(); print('ok' if a.api_key else 'no')" 2>/dev/null | grep -q ok; then
            echo -e "  云端:     ${GREEN}✅ DeepSeek 已配置${NC}"
        else
            echo -e "  云端:     ${YELLOW}⚠️  未配置${NC}"
        fi
        # 协议
        if [ -f /etc/tork/.agreed ]; then
            echo -e "  协议:     ${GREEN}✅ 已签署${NC}"
        else
            echo -e "  协议:     ${DIM}⏹️  未签署${NC}"
        fi
        # 世代
        EVO_FILE="$BASE_DIR/persist/evolution.json"
        if [ -f "$EVO_FILE" ]; then
            GEN=$(python3 -c "import json; d=json.load(open('$EVO_FILE')); print(d[-1].get('generation','?'))" 2>/dev/null || echo "?")
            echo -e "  世代:     ${CYAN}${GEN}${NC}"
        fi
        ;;

    stop)
        echo -e "${YELLOW}⏹️  停止 TORK...${NC}"
        # 停止引擎
        for p in tork_engine tork_core; do
            if pgrep -x "$p" > /dev/null 2>&1; then
                pkill -x "$p" 2>/dev/null
                echo -e "  ⏹️  已停止 $p"
            fi
        done
        # 停止仪表盘
        if pgrep -f tork_dashboard.py > /dev/null 2>&1; then
            pkill -f tork_dashboard.py 2>/dev/null
            echo -e "  ⏹️  已停止仪表盘"
        fi
        # 停止守护进程
        if [ -f "$BASE_DIR/persist/daemon.pid" ]; then
            DPID=$(cat "$BASE_DIR/persist/daemon.pid")
            kill "$DPID" 2>/dev/null || true
            rm -f "$BASE_DIR/persist/daemon.pid"
            echo -e "  ⏹️  已停止守护进程"
        fi
        echo -e "${GREEN}✅ TORK 已停止${NC}"
        ;;

    restart)
        "$0" stop
        sleep 1
        "$0" start
        ;;

    compile|build)
        echo -e "${GREEN}🔧 编译 TORK...${NC}"
        make -C "$BASE_DIR" all
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}✅ 编译成功${NC}"
        else
            echo -e "${RED}❌ 编译失败${NC}"
            exit 1
        fi
        ;;

    evolve)
        echo -e "${CYAN}🧬 运行进化...${NC}"
        python3 "$EVOLUTION" --once
        ;;

    protocol)
        echo -e "${CYAN}☁️  启动云端协议 (交互模式)${NC}"
        echo -e "${DIM}   输入 JSON 指令，Ctrl+D 退出${NC}"
        python3 "$PROTOCOL"
        ;;

    log)
        echo -e "${CYAN}📜 进化日志${NC}"
        if [ -f "$BASE_DIR/persist/evolution.json" ]; then
            python3 -c "
import json
d = json.load(open('$BASE_DIR/persist/evolution.json'))
for e in d[-20:]:
    gen = e.get('generation', '?')
    f = e.get('file', '?')
    s = e.get('status', '?')
    desc = e.get('description', '')
    print(f'  Gen {gen:>3} | {f:<20} | {s:>7} | {desc}')
"
        else
            echo -e "  ${DIM}(无进化记录)${NC}"
        fi
        ;;

    help|*)
        echo -e "${CYAN}🥚 TORK — 使用说明${NC}"
        echo -e "${DIM}──────────────────────────────${NC}"
        echo -e "  ${GREEN}start${NC}       启动引擎 + 仪表盘"
        echo -e "  ${GREEN}dashboard${NC}   仅启动仪表盘"
        echo -e "  ${GREEN}engine${NC}      仅启动引擎"
        echo -e "  ${GREEN}daemon${NC}      后台守护进程"
        echo -e "  ${GREEN}status${NC}      查看运行状态"
        echo -e "  ${GREEN}stop${NC}        停止所有进程"
        echo -e "  ${GREEN}restart${NC}     重启"
        echo -e "  ${GREEN}compile${NC}     编译项目"
        echo -e "  ${GREEN}evolve${NC}      运行一次进化"
        echo -e "  ${GREEN}protocol${NC}    启动云端协议交互"
        echo -e "  ${GREEN}log${NC}         查看进化日志"
        echo -e "${DIM}──────────────────────────────${NC}"
        ;;
esac
