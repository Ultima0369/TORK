#!/bin/bash
# ╔══════════════════════════════════════════════════════╗
# ║  TORK · 悬浮窗启动器                                  ║
# ║  用法: ./tork.sh [start|stop|restart|status|connect]  ║
# ╚══════════════════════════════════════════════════════╝

set -e
BASE="$(cd "$(dirname "$0")" && pwd)"
FLOAT_PID_FILE="/tmp/tork_floating.pid"
HK_PID_FILE="/tmp/tork_hotkey.pid"
SIGNAL_FILE="/tmp/tork.flag"

case "${1:-start}" in
    start)
        echo "🦀 TORK 悬浮窗启动中..."
        
        # 检查 API Key
        KEY="${DEEPSEEK_API_KEY:-}"
        if [ -z "$KEY" ] && [ -f "$BASE/.tork_floating.json" ]; then
            KEY=$(python3 -c "import json; print(json.load(open('$BASE/.tork_floating.json')).get('api_key',''))" 2>/dev/null || echo "")
        fi
        if [ -z "$KEY" ]; then
            echo "   ⚠ 未设置 DeepSeek API Key"
            echo "     运行 ./tork.sh connect 配置"
        fi

        # 启动 TORK Core（如果不在运行）
        if ! pgrep -x tork_core > /dev/null 2>&1; then
            if [ -f "$BASE/build/tork_core" ]; then
                nohup "$BASE/build/tork_core" > /dev/null 2>&1 &
                echo "   ❤ TORK Core 已启动"
            else
                echo "   ⚠ TORK Core 未编译，运行 ./build.sh 编译"
            fi
        else
            echo "   ❤ TORK Core 已在运行"
        fi

        # 启动热键守护进程（evdev 版，Wayland 原生）
        if [ ! -f "$HK_PID_FILE" ] || ! kill -0 "$(cat "$HK_PID_FILE" 2>/dev/null)" 2>/dev/null; then
            setsid python3 "$BASE/tork_hotkey.py" < /dev/null > /tmp/tork_hotkey.log 2>&1 &
            echo "   🔑 热键守护进程已启动 (左 Ctrl + 左 Shift + T)"
        else
            echo "   🔑 热键守护进程已在运行"
        fi

        # 启动浮窗
        if [ ! -f "$FLOAT_PID_FILE" ] || ! kill -0 "$(cat "$FLOAT_PID_FILE" 2>/dev/null)" 2>/dev/null; then
            setsid python3 "$BASE/tork_floating.py" < /dev/null > /tmp/tork_float.log 2>&1 &
            echo "   🪟 悬浮窗已启动"
        else
            echo "   🪟 悬浮窗已在运行"
        fi

        echo ""
        echo "✅ TORK 就绪 · 按 左 Ctrl + 左 Shift + T 唤出"
        ;;

    stop)
        echo "⏹ TORK 停止中..."
        if [ -f "$FLOAT_PID_FILE" ]; then
            kill "$(cat "$FLOAT_PID_FILE")" 2>/dev/null || true
            rm -f "$FLOAT_PID_FILE"
        fi
        if [ -f "$HK_PID_FILE" ]; then
            kill "$(cat "$HK_PID_FILE")" 2>/dev/null || true
            rm -f "$HK_PID_FILE"
        fi
        pkill -f "tork_floating" 2>/dev/null || true
        pkill -f "tork_hotkey" 2>/dev/null || true
        rm -f "$SIGNAL_FILE"
        echo "✅ TORK 已停止"
        ;;

    restart)
        "$0" stop
        sleep 0.3
        "$0" start
        ;;

    status)
        echo "📊 TORK 状态"
        echo "============"
        if [ -f "$HK_PID_FILE" ] && kill -0 "$(cat "$HK_PID_FILE")" 2>/dev/null; then
            echo "   🔑 热键守护进程: ✅ (PID $(cat "$HK_PID_FILE"))"
        else
            echo "   🔑 热键守护进程: ❌"
        fi
        if [ -f "$FLOAT_PID_FILE" ] && kill -0 "$(cat "$FLOAT_PID_FILE")" 2>/dev/null; then
            echo "   🪟 悬浮窗: ✅ (PID $(cat "$FLOAT_PID_FILE"))"
        else
            echo "   🪟 悬浮窗: ❌"
        fi
        if pgrep -x tork_core > /dev/null 2>&1; then
            echo "   ❤ TORK Core: ✅"
        else
            echo "   ❤ TORK Core: ❌ (未运行)"
        fi
        if [ -f "$SIGNAL_FILE" ]; then
            echo "   📨 信号文件: $(cat "$SIGNAL_FILE")"
        else
            echo "   📨 信号文件: (无)"
        fi
        ;;

    connect)
        echo "🔑 TORK API Key 配置"
        echo "===================="
        echo ""
        echo "输入你的 DeepSeek API Key (输入后按回车):"
        read -rs key
        echo ""
        if [ -z "$key" ]; then
            echo "❌ Key 不能为空"
            exit 1
        fi
        # 测试
        echo "测试中..."
        test_resp=$(curl -s -w "\n%{http_code}" https://api.deepseek.com/v1/models \
            -H "Authorization: Bearer $key" --connect-timeout 5)
        http_code=$(echo "$test_resp" | tail -1)
        if [ "$http_code" = "200" ]; then
            echo "✅ API Key 验证通过"
        else
            echo "⚠ API 测试返回 HTTP $http_code (可能 Key 无效)"
        fi
        # 保存
        cat > "$BASE/.tork_floating.json" << JSONEOF
{
    "hotkey": "Control+Shift+T",
    "auto_hide_sec": 0,
    "font_size": 14,
    "opacity": 0.93,
    "model": "deepseek-chat",
    "api_key": "$key"
}
JSONEOF
        # 写入环境变量
        export DEEPSEEK_API_KEY="$key"
        if ! grep -q "DEEPSEEK_API_KEY" ~/.bashrc 2>/dev/null; then
            echo "export DEEPSEEK_API_KEY='$key'" >> ~/.bashrc
        fi
        echo "✅ API Key 已保存到 .tork_floating.json"
        echo "   运行 ./tork.sh restart 重新加载"
        ;;

    toggle)
        echo "toggle" > "$SIGNAL_FILE"
        echo "📨 切换信号已发送"
        ;;

    *)
        echo "用法: $0 [start|stop|restart|status|connect|toggle]"
        ;;
esac
