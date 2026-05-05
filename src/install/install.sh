#!/bin/bash
# ──────────────────────────────────────────────────────
# TORK AI — Setup Wizard
# Usage: ./install.sh
# ──────────────────────────────────────────────────────

set -e

BASE="$(cd "$(dirname "$0")/.." && pwd)"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
DIM='\033[2m'
NC='\033[0m'

echo ""
echo -e "${CYAN}TORK AI — Setup Wizard${NC}"
echo -e "${DIM}─────────────────────────────${NC}"
echo ""

# ── EULA ──────────────────────────────────────────────────────
echo -e "${CYAN}End User License Agreement${NC}"
echo -e "${DIM}─────────────────────────────${NC}"
echo ""
echo "  1. GRANT OF LICENSE"
echo "     TORK AI is licensed, not sold. You may install and run"
echo "     TORK AI on your local machine for personal or commercial use."
echo ""
echo "  2. SCOPE OF USE"
echo "     TORK AI provides code analysis, generation, and system"
echo "     management capabilities. You choose the permission level."
echo ""
echo "  3. RESOURCE USAGE"
echo "     TORK AI uses local compute and storage resources."
echo ""
echo "  4. LIABILITY"
echo "     TORK AI is provided AS IS without warranty."
echo ""
echo "  5. TERMINATION"
echo "     You may uninstall TORK AI at any time."
echo ""

while true; do
    read -p "Do you accept the EULA? [y/N]: " accept
    case $accept in
        y|Y)
            break
            ;;
        *)
            echo ""
            echo -e "${YELLOW}EULA not accepted. Setup cancelled.${NC}"
            echo ""
            exit 0
            ;;
    esac
done

echo ""
echo -e "${GREEN}EULA accepted.${NC}"
echo ""

# ── Permission level ──────────────────────────────────────────
echo -e "${CYAN}Permission Level${NC}"
echo -e "${DIM}─────────────────────────────${NC}"
echo ""
echo "  1) Read-only  — file and system information access"
echo "     (ls, cat, ps, find, grep...)"
echo ""
echo "  2) Standard   — read/write files, compile code"
echo "     (+ file editing, gcc, git, python...)"
echo ""
echo "  3) Full       — system service management"
echo "     (+ apt, systemctl, network tools...)"
echo ""
echo "  0) Decline    — cancel setup"
echo ""

while true; do
    read -p "Select permission level [0/1/2/3]: " choice
    case $choice in
        0)
            echo ""
            echo -e "${YELLOW}Setup cancelled.${NC}"
            echo ""
            exit 0
            ;;
        1)
            LEVEL="read"
            LEVEL_NUM=1
            break
            ;;
        2)
            LEVEL="normal"
            LEVEL_NUM=3
            break
            ;;
        3)
            LEVEL="full"
            LEVEL_NUM=4
            break
            ;;
        *)
            echo "Enter 0, 1, 2, or 3"
            ;;
    esac
done

echo ""
echo -e "${GREEN}Permission level: ${LEVEL}${NC}"
echo ""

# ── Check root ────────────────────────────────────────────────
if [ "$(id -u)" != "0" ]; then
    echo -e "${YELLOW}Writing agreement requires root. Using sudo...${NC}"
    exec sudo "$0" "$@"
    exit 0
fi

# ── Write agreement ───────────────────────────────────────────
mkdir -p /etc/tork

cat > /etc/tork/agreement.json << JSONEOF
{
    "version": 1,
    "state": "accepted",
    "sandbox_level": $LEVEL_NUM,
    "agreed_at": $(date +%s),
    "expires_at": 0,
    "label": "$LEVEL"
}
JSONEOF

touch /etc/tork/.agreed
echo -e "${GREEN}Agreement written to /etc/tork/agreement.json${NC}"

# ── Build TORK ────────────────────────────────────────────────
echo ""
echo -e "${CYAN}Building TORK...${NC}"
cd "$BASE"

DEPS_OK=true
for cmd in gcc as ld make python3; do
    if ! command -v "$cmd" &>/dev/null; then
        echo -e "${RED}  Missing dependency: $cmd${NC}"
        DEPS_OK=false
    fi
done

if [ "$DEPS_OK" = false ]; then
    echo -e "${RED}Install missing dependencies and retry.${NC}"
    exit 1
fi

echo -e "${GREEN}  All dependencies satisfied${NC}"

make clean 2>/dev/null || true
make all 2>&1 | while read line; do echo "  $line"; done

if [ -f "$BASE/build/tork_core" ] && [ -f "$BASE/build/tork_engine" ]; then
    echo -e "${GREEN}  Build successful${NC}"
else
    echo -e "${RED}  Build failed${NC}"
    exit 1
fi

# ── API Key (optional) ────────────────────────────────────────
echo ""
echo -e "${CYAN}API Key (optional)${NC}"
echo ""
echo "TORK AI can connect to cloud AI services (DeepSeek/Claude/GPT)"
echo "for enhanced capabilities. You can configure this later with ./tork.sh connect."
echo ""
read -p "Configure API Key now? [y/N]: " config_api
if [ "$config_api" = "y" ] || [ "$config_api" = "Y" ]; then
    read -p "Enter DeepSeek API Key: " apikey
    if [ -n "$apikey" ]; then
        cat > "$BASE/.tork_floating.json" << JSONEOF
{
    "hotkey": "Control+Shift+T",
    "auto_hide_sec": 0,
    "font_size": 14,
    "opacity": 0.93,
    "model": "deepseek-chat",
    "api_key": "$apikey"
}
JSONEOF
        echo -e "${GREEN}  API Key saved${NC}"
        export DEEPSEEK_API_KEY="$apikey"
        if ! grep -q "DEEPSEEK_API_KEY" ~/.bashrc 2>/dev/null; then
            echo "export DEEPSEEK_API_KEY='$apikey'" >> ~/.bashrc
        fi
    fi
fi

# ── Done ──────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}TORK AI setup complete${NC}"
echo -e "${DIM}─────────────────────────────${NC}"
echo ""
echo "  Start:     ./tork.sh start"
echo "  Status:    ./tork.sh status"
echo "  Dashboard: ./tork.sh dashboard"
echo "  Connect:   ./tork.sh connect"
echo ""
