#!/bin/bash
# TORK 看门狗模式 — 一键启动
# 用法: ./tork_watchdog.sh [start|stop|status]

PIDFILE=/tmp/tork_watchdogd.pid
GUI_PIDFILE=/tmp/tork_floating.pid

start() {
    echo "TORK 看门狗启动..."
    
    # 启动后台看门狗
    python3 "$(dirname "$0")/tork_watchdogd.py" &
    WDPID=$!
    echo $WDPID > $PIDFILE
    echo "  看门狗 PID: $WDPID"
    
    # 启动悬浮窗
    python3 "$(dirname "$0")/tork_floating.py" --show &
    GUIPID=$!
    echo $GUIPID > $GUI_PIDFILE
    echo "  悬浮窗 PID: $GUIPID"
    
    echo "TORK 看门狗模式已启动。"
    echo "  开关窗口: 按 Ctrl+Shift+T (如果配置了热键)"
    echo "  或运行: ./tork_watchdog.sh toggle"
}

stop() {
    echo "TORK 看门狗停止..."
    if [ -f $PIDFILE ]; then
        kill $(cat $PIDFILE) 2>/dev/null
        rm -f $PIDFILE
        echo "  看门狗已停止"
    fi
    if [ -f $GUI_PIDFILE ]; then
        kill $(cat $GUI_PIDFILE) 2>/dev/null
        rm -f $GUI_PIDFILE
        echo "  悬浮窗已关闭"
    fi
    rm -f /tmp/tork_watchdog.alert
    echo "TORK 已停止。"
}

status() {
    echo "TORK 看门狗状态:"
    if [ -f $PIDFILE ] && kill -0 $(cat $PIDFILE) 2>/dev/null; then
        echo "  看门狗: ✅ PID=$(cat $PIDFILE)"
    else
        echo "  看门狗: ❌ 未运行"
    fi
    if [ -f $GUI_PIDFILE ] && kill -0 $(cat $GUI_PIDFILE) 2>/dev/null; then
        echo "  悬浮窗: ✅ PID=$(cat $GUI_PIDFILE)"
    else
        echo "  悬浮窗: ❌ 未运行"
    fi
    if [ -f /tmp/tork_watchdog.alert ]; then
        echo "  警报: ⚠ 有未处理的警报"
    else
        echo "  警报: ✓ 无警报"
    fi
}

toggle() {
    if [ -f $GUI_PIDFILE ] && kill -0 $(cat $GUI_PIDFILE) 2>/dev/null; then
        python3 -c "
import os, sys
sys.path.insert(0, '$(dirname "$0")')
os.chdir('$(dirname "$0")')
# 通过 flag 文件切换
with open('/tmp/tork.flag', 'w') as f: f.write('toggle')
" 2>/dev/null
    else
        start
    fi
}

case "$1" in
    start)   start ;;
    stop)    stop ;;
    status)  status ;;
    toggle)  toggle ;;
    restart) stop; sleep 1; start ;;
    *)
        echo "用法: $0 {start|stop|status|toggle|restart}"
        echo "  start   - 启动看门狗 + 悬浮窗"
        echo "  stop    - 停止所有"
        echo "  status  - 查看状态"
        echo "  toggle  - 切换窗口显示"
        exit 1
        ;;
esac
