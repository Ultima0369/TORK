#!/bin/bash
# 🥚 TORK 守护进程控制脚本
# 用法: tools/torkd_ctl.sh [start|stop|status|restart|query]
BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$BASE_DIR/build/tork_engine"
PIDFILE="/tmp/tork_engine.pid"
LOGFILE="$BASE_DIR/persist/torkd.log"

mkdir -p "$BASE_DIR/persist"

case "${1:-help}" in
    start)
        if [ -f "$PIDFILE" ] && kill -0 $(cat "$PIDFILE") 2>/dev/null; then
            echo "TORK 已在运行 (PID: $(cat $PIDFILE))"
            exit 0
        fi
        echo "🥚 TORK 守护进程启动中..."
        nohup "$ENGINE" --daemon --quiet 20000000 > "$LOGFILE" 2>&1 &
        echo $! > "$PIDFILE"
        sleep 1
        if kill -0 $(cat "$PIDFILE") 2>/dev/null; then
            echo "✅ TORK 已启动 (PID: $(cat $PIDFILE))"
            echo "   日志: $LOGFILE"
        else
            echo "❌ TORK 启动失败"
            rm -f "$PIDFILE"
            exit 1
        fi
        ;;
    stop)
        echo "⏹️  停止 TORK..."
        if [ -f "$PIDFILE" ]; then
            kill $(cat "$PIDFILE") 2>/dev/null
            sleep 1
            rm -f "$PIDFILE"
        fi
        # 确保所有相关进程已停止
        pkill -x tork_engine 2>/dev/null
        pkill -x tork_core 2>/dev/null
        echo "✅ TORK 已停止"
        ;;
    status)
        if [ -f "$PIDFILE" ] && kill -0 $(cat "$PIDFILE") 2>/dev/null; then
            PID=$(cat "$PIDFILE")
            UPTIME=$(ps -o etime= -p $PID 2>/dev/null | tr -d ' ')
            echo "✅ TORK 运行中 (PID: $PID, 运行时间: ${UPTIME:-?})"
            # 检查 socket
            if [ -S /tmp/torkd.sock ]; then
                echo "📡 Socket: /tmp/torkd.sock"
                # 尝试查询
                if command -v tools/tork_query.sh &>/dev/null; then
                    tools/tork_query.sh status 2>/dev/null || echo "   (查询不可达)"
                fi
            fi
        else
            echo "⏹️  TORK 未运行"
            rm -f "$PIDFILE"
        fi
        ;;
    restart)
        $0 stop
        sleep 1
        $0 start
        ;;
    query)
        if [ ! -S /tmp/torkd.sock ]; then
            echo "TORK socket 不可用"
            exit 1
        fi
        REQ="${2:-status}"
        echo "$REQ" | socat - UNIX-CONNECT:/tmp/torkd.sock 2>/dev/null || \
            echo "查询失败 (socat 未安装或 socket 不可用)"
        ;;
    log)
        tail -20 "$LOGFILE"
        ;;
    *)
        echo "用法: $0 {start|stop|status|restart|query|log}"
        ;;
esac
