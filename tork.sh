#!/bin/bash
# ──────────────────────────────────────────────────────
# TORK AI — 自进化智能引擎
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
        echo -e "${GREEN}TORK engine starting...${NC}"
        if [ ! -f "$ENGINE" ]; then
            echo -e "${YELLOW}Engine not compiled, building...${NC}"
            make -C "$BASE_DIR" all
        fi
        python3 "$DAEMON" all
        ;;

    dashboard)
        echo -e "${GREEN}Opening TORK web dashboard...${NC}"
        python3 "$BASE_DIR/web/tork_web.py" "$@" &
        ;;

    engine)
        echo -e "${GREEN}Starting TORK engine...${NC}"
        if [ ! -f "$ENGINE" ]; then
            echo -e "${YELLOW}Engine not compiled, building...${NC}"
            make -C "$BASE_DIR" all
        fi
        "$ENGINE"
        ;;

    daemon)
        echo -e "${GREEN}Starting TORK daemon (background)...${NC}"
        python3 "$DAEMON" all &
        echo -e "   PID: $!"
        ;;

    status)
        echo -e "${CYAN}TORK Status${NC}"
        echo -e "${DIM}────────────────────────${NC}"
        # Engine
        if pgrep -x tork_core > /dev/null 2>&1; then
            echo -e "  Engine:    ${GREEN}running${NC} (PID: $(pgrep -x tork_core))"
        elif pgrep -x tork_engine > /dev/null 2>&1; then
            echo -e "  Engine:    ${GREEN}running${NC} (PID: $(pgrep -x tork_engine))"
        else
            echo -e "  Engine:    ${RED}stopped${NC}"
        fi
        # Dashboard
        if pgrep -f tork_web.py > /dev/null 2>&1; then
            echo -e "  Dashboard: ${GREEN}running${NC} (web)"
        elif pgrep -f tork_dashboard.py > /dev/null 2>&1; then
            echo -e "  Dashboard: ${GREEN}running${NC} (tkinter)"
        else
            echo -e "  Dashboard: ${DIM}not running${NC}"
        fi
        # Daemon
        if [ -f "$BASE_DIR/persist/daemon.pid" ]; then
            DPID=$(cat "$BASE_DIR/persist/daemon.pid")
            if kill -0 "$DPID" 2>/dev/null; then
                echo -e "  Daemon:    ${GREEN}running${NC} (PID: $DPID)"
            else
                echo -e "  Daemon:    ${YELLOW}stale PID${NC}"
            fi
        else
            echo -e "  Daemon:    ${DIM}not running${NC}"
        fi
        # Cloud API
        if python3 -c "from tork_api import TorkAPI; a=TorkAPI(); print('ok' if a.api_key else 'no')" 2>/dev/null | grep -q ok; then
            echo -e "  Cloud API: ${GREEN}configured${NC}"
        else
            echo -e "  Cloud API: ${YELLOW}not configured${NC}"
        fi
        # License
        if [ -f /etc/tork/.agreed ]; then
            echo -e "  License:   ${GREEN}accepted${NC}"
        else
            echo -e "  License:   ${DIM}not accepted${NC}"
        fi
        # Generation
        EVO_FILE="$BASE_DIR/persist/evolution.json"
        if [ -f "$EVO_FILE" ]; then
            GEN=$(python3 -c "import json; d=json.load(open('$EVO_FILE')); print(d[-1].get('generation','?'))" 2>/dev/null || echo "?")
            echo -e "  Generation: ${CYAN}${GEN}${NC}"
        fi
        ;;

    stop)
        echo -e "${YELLOW}Stopping TORK...${NC}"
        for p in tork_engine tork_core; do
            if pgrep -x "$p" > /dev/null 2>&1; then
                pkill -x "$p" 2>/dev/null
                echo -e "  Stopped $p"
            fi
        done
        if pgrep -f tork_dashboard.py > /dev/null 2>&1; then
            pkill -f tork_dashboard.py 2>/dev/null
            echo -e "  Stopped dashboard"
        fi
        if pgrep -f tork_web.py > /dev/null 2>&1; then
            pkill -f tork_web.py 2>/dev/null
            echo -e "  Stopped web dashboard"
        fi
        if [ -f "$BASE_DIR/persist/daemon.pid" ]; then
            DPID=$(cat "$BASE_DIR/persist/daemon.pid")
            kill "$DPID" 2>/dev/null || true
            rm -f "$BASE_DIR/persist/daemon.pid"
            echo -e "  Stopped daemon"
        fi
        echo -e "${GREEN}TORK stopped${NC}"
        ;;

    restart)
        "$0" stop
        sleep 1
        "$0" start
        ;;

    compile|build)
        echo -e "${GREEN}Building TORK...${NC}"
        make -C "$BASE_DIR" all
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}Build successful${NC}"
        else
            echo -e "${RED}Build failed${NC}"
            exit 1
        fi
        ;;

    evolve)
        echo -e "${CYAN}Running evolution...${NC}"
        python3 "$EVOLUTION" --once
        ;;

    protocol)
        echo -e "${CYAN}Starting cloud protocol (interactive)${NC}"
        echo -e "${DIM}   Enter JSON commands, Ctrl+D to exit${NC}"
        python3 "$PROTOCOL"
        ;;

    log)
        echo -e "${CYAN}Evolution Log${NC}"
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
            echo -e "  ${DIM}(no evolution records)${NC}"
        fi
        ;;

    help|*)
        echo -e "${CYAN}TORK AI — Command Reference${NC}"
        echo -e "${DIM}──────────────────────────────${NC}"
        echo -e "  ${GREEN}start${NC}       Start engine + dashboard"
        echo -e "  ${GREEN}dashboard${NC}   Open dashboard only"
        echo -e "  ${GREEN}engine${NC}      Start engine only"
        echo -e "  ${GREEN}daemon${NC}      Start daemon (background)"
        echo -e "  ${GREEN}status${NC}      Show running status"
        echo -e "  ${GREEN}stop${NC}        Stop all processes"
        echo -e "  ${GREEN}restart${NC}     Restart"
        echo -e "  ${GREEN}compile${NC}     Build project"
        echo -e "  ${GREEN}evolve${NC}      Run one evolution cycle"
        echo -e "  ${GREEN}protocol${NC}    Start cloud protocol (interactive)"
        echo -e "  ${GREEN}log${NC}         View evolution log"
        echo -e "${DIM}──────────────────────────────${NC}"
        ;;
esac