#!/bin/bash
# ══════════════════════════════════════════════════════════════
# TORK 树莓派一键部署脚本
#
# 用法: curl -sL https://tinyurl.com/tork-rpi | sudo bash
# 或:   bash rpi_deploy.sh [--sensors] [--battery]
#
# 选项:
#   --sensors   启用 I2C 传感器支持 (DS18B20, INA219, etc.)
#   --battery   电池组监控模式 (默认: 通用边缘模式)
#   --ev       电动汽车模式 (CAN bus + 电机传感器)
# ══════════════════════════════════════════════════════════════

set -e

# ── 颜色 ────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; NC='\033[0m'

echo -e "${BLUE}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     TORK 树莓派边缘部署                        ║${NC}"
echo -e "${BLUE}║     自进化预警系统                              ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════╝${NC}"

# ── 检测架构 ────────────────────────────────────────────
ARCH=$(uname -m)
case "$ARCH" in
    aarch64|armv7l|armv6l)
        echo -e "${GREEN}✓ 架构: $ARCH (ARM)${NC}" ;;
    *)
        echo -e "${YELLOW}⚠ 非 ARM 架构 ($ARCH)，可能不兼容${NC}" ;;
esac

# ── 安装依赖 ────────────────────────────────────────────
echo -e "\n${BLUE}→ 安装依赖...${NC}"
apt-get update -qq
apt-get install -y -qq build-essential make gcc libc6-dev \
    i2c-tools 2>/dev/null || true

# I2C 启用
if lsmod 2>/dev/null | grep -q i2c_dev || [ -e /dev/i2c-1 ]; then
    echo -e "${GREEN}✓ I2C 已启用${NC}"
else
    echo -e "${YELLOW}⚠ I2C 未启用, 尝试启用...${NC}"
    raspi-config nonint do_i2c 0 2>/dev/null || true
    echo "dtparam=i2c_arm=on" >> /boot/config.txt 2>/dev/null || true
fi

# ── 获取源码 ────────────────────────────────────────────
echo -e "\n${BLUE}→ 获取 TORK 源码...${NC}"
if [ ! -d /opt/tork ]; then
    git clone --depth 1 https://github.com/Ultima0369/TORK.git /opt/tork 2>/dev/null || {
        echo -e "${RED}✗ 克隆失败, 请检查网络${NC}"
        exit 1
    }
else
    cd /opt/tork && git pull --ff-only 2>/dev/null || true
fi
cd /opt/tork

# ── 编译边缘版本 ──────────────────────────────────────────
echo -e "\n${BLUE}→ 编译 TORK 边缘版 (DTORK_EDGE=1)...${NC}"
make clean 2>/dev/null || true
make CFLAGS_EXTRA="-DTORK_EDGE=1 -DTORK_NO_ASM -Os -s" -j$(nproc) || {
    echo -e "${RED}✗ 编译失败, 请检查依赖${NC}"
    exit 1
}
echo -e "${GREEN}✓ 编译成功${NC}"

# ── 模式配置 ────────────────────────────────────────────
MODE="通用边缘"
SENSOR_FLAGS=""
case "$1" in
    --sensors)
        MODE="传感器监控"
        SENSOR_FLAGS="-DTORK_EDGE_SENSORS=1"
        # 启用 DS18B20
        echo "dtoverlay=w1-gpio" >> /boot/config.txt 2>/dev/null || true
        ;;
    --battery)
        MODE="电池组监控"
        SENSOR_FLAGS="-DTORK_EDGE_BATTERY=1"
        # INA219 电压/电流传感器
        echo -e "${YELLOW}  配置电池传感器 (INA219/I2C)${NC}"
        ;;
    --ev)
        MODE="电动汽车"
        SENSOR_FLAGS="-DTORK_EDGE_EV=1"
        echo -e "${YELLOW}  配置 CAN bus 接口 (MCP2515)${NC}"
        ;;
esac

# 如有传感器标志, 重新编译
if [ -n "$SENSOR_FLAGS" ]; then
    make clean 2>/dev/null || true
    make CFLAGS_EXTRA="-DTORK_EDGE=1 $SENSOR_FLAGS -DTORK_NO_ASM -Os -s" -j$(nproc)
    echo -e "${GREEN}✓ 重编译完成 (模式: $MODE)${NC}"
fi

# ── 安装为系统服务 ──────────────────────────────────────
echo -e "\n${BLUE}→ 安装 systemd 服务...${NC}"
cat > /etc/systemd/system/tork-edge.service << 'EOF'
[Unit]
Description=TORK Edge - Self-Evolving Anomaly Predictor
After=network.target i2c.target

[Service]
Type=simple
ExecStart=/opt/tork/build/tork_engine --foreground --edge
WorkingDirectory=/opt/tork
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable tork-edge.service
systemctl start tork-edge.service || true

echo -e "${GREEN}✓ TORK 边缘服务已安装并启动${NC}"

# ── 验证 ────────────────────────────────────────────────
sleep 2
if systemctl is-active --quiet tork-edge.service; then
    echo -e "\n${GREEN}╔══════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║  TORK 边缘部署成功!                             ║${NC}"
    echo -e "${GREEN}║  模式: $MODE${NC}"
    echo -e "${GREEN}║  状态: 运行中                                    ║${NC}"
    echo -e "${GREEN}║  日志: journalctl -u tork-edge -f               ║${NC}"
    echo -e "${GREEN}║  配置: /opt/tork/src/config.h                    ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════════════╝${NC}"
else
    echo -e "\n${YELLOW}⚠ 服务未运行, 检查日志: journalctl -u tork-edge${NC}"
fi

# ── 预警测试 ────────────────────────────────────────────
echo -e "\n${BLUE}→ 运行快速预警测试...${NC}"
/opt/tork/build/tork_engine --test-predictor 2>/dev/null || true
echo -e "${GREEN}✓ 系统就绪${NC}"

echo -e "\n${YELLOW}提示: 调整敏感度 → echo 'sensitivity 80' | nc localhost 9876${NC}"
