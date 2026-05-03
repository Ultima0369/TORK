#!/bin/bash
# ╔══════════════════════════════════════════════════════╗
# ║  TORK · 悬浮窗启动器                                  ║
# ║  用法: ./tork.sh [start|stop|restart|status|connect]  ║
# ╚══════════════════════════════════════════════════════╝

BASE="$(cd "$(dirname "$0")" && pwd)"
PID_FILE="/tmp/tork_floating.pid"
HK_PID_FILE="/tmp/tork_hotkey.pid"

case "${1:-start}" in
    start)
        echo "🦀 TORK 悬浮窗启动中..."
        
        # 1. 检查 TORK Core 是否在运行，不在则启动
        if ! pgrep -x tork_core > /dev/null 2>&1; then
            if [ -f "$BASE/build/tork_core" ]; then
                echo "   ▶ 启动 TORK Core..."
                "$BASE/build/tork_core" &
                sleep 0.5
            fi
        else
            echo "   ❤ TORK Core 已在运行"
        fi
        
        # 2. 启动热键守护进程（后台）
        if [ ! -f "$HK_PID_FILE" ] || ! kill -0 $(cat "$HK_PID_FILE") 2>/dev/null; then
            nohup python3 "$BASE/tork_hotkey.py" > /dev/null 2>&1 &
            echo "   🔑 热键守护进程已启动 (Ctrl+Shift+T)"
        else
            echo "   🔑 热键守护进程已在运行"
        fi
        
        # 3. 启动悬浮窗（隐藏状态，热键唤出）
        if [ ! -f "$PID_FILE" ] || ! kill -0 $(cat "$PID_FILE") 2>/dev/null; then
            nohup python3 "$BASE/tork_floating.py" > /dev/null 2>&1 &
            FLOAT_PID=$!
            echo "$FLOAT_PID" > "$PID_FILE"
            echo "   🪟 悬浮窗已启动 (PID $FLOAT_PID)"
        else
            echo "   🪟 悬浮窗已在运行"
        fi
        
        echo "✅ TORK 就绪 — 按 Ctrl+Shift+T 唤出"
        ;;
    
    stop)
        echo "⏹ TORK 停止中..."
        if [ -f "$HK_PID_FILE" ]; then
            kill $(cat "$HK_PID_FILE") 2>/dev/null && echo "   🔑 热键守护进程已停止"
            rm -f "$HK_PID_FILE"
        fi
        if [ -f "$PID_FILE" ]; then
            kill $(cat "$PID_FILE") 2>/dev/null && echo "   🪟 悬浮窗已停止"
            rm -f "$PID_FILE"
        fi
        # 清理 socket
        rm -f /tmp/tork.flag
        echo "✅ TORK 已停止"
        ;;
    
    restart)
        "$0" stop
        sleep 1
        "$0" start
        ;;
    
    status)
        echo "📊 TORK 状态"
        echo "━━━━━━━━━━━━━━━"
        if pgrep -x tork_core > /dev/null 2>&1; then
            echo "  ❤ Core:    运行中 ($(pgrep -x tork_core))"
        else
            echo "  💤 Core:    未运行"
        fi
        if [ -f "$PID_FILE" ] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
            echo "  🪟 浮窗:   运行中 ($(cat $PID_FILE))"
        else
            echo "  🪟 浮窗:   未启动"
        fi
        if [ -f "$HK_PID_FILE" ] && kill -0 $(cat "$HK_PID_FILE") 2>/dev/null; then
            echo "  🔑 热键:   运行中 ($(cat $HK_PID_FILE))"
        else
            echo "  🔑 热键:   未启动"
        fi
        if [ -f /tmp/tork.flag ]; then
            echo "  🚩 信号:   有未处理的信号"
        else
            echo "  🚩 信号:   无"
        fi
        echo ""
        echo "  按 Ctrl+Shift+T 唤出悬浮窗"
        ;;
    
    connect)
        # 手动连接 API
        echo "🌐 连接 DeepSeek API..."
        read -sp "API Key: " key
        echo
        export DEEPSEEK_API_KEY="$key"
        # 保存到配置
        cat > "$BASE/.tork_floating.json" << EOF
{
    "opacity": 0.92,
    "theme": "dark",
    "hotkey": "Control+Shift+T",
    "auto_hide_sec": 5,
    "font_size": 14,
    "height": 48,
    "width": 520,
    "api_key": "$key"
}
EOF
        # 持久化到 bashrc
        if ! grep -q "DEEPSEEK_API_KEY" ~/.bashrc; then
            echo "export DEEPSEEK_API_KEY='$key'" >> ~/.bashrc
        fi
        echo "✅ API Key 已保存"
        ;;
    
    *)
        echo "用法: $0 [start|stop|restart|status|connect]"
        ;;
esac
