#!/bin/bash
# TORK 快速查询 (不需要 socat)
SOCKET="/tmp/torkd.sock"
REQ="${1:-status}"

if [ ! -S "$SOCKET" ]; then
    echo "TORK socket 不可用 ($SOCKET)"
    exit 1
fi

# 使用 /dev/tcp 发送查询
exec 3<>/dev/tcp/localhost/$(lsof -U "$SOCKET" 2>/dev/null | awk 'NR>1{print $9}' | head -1 || echo "0") 2>/dev/null

# 如果没有 socat 也没有 bash tcp，用 python
python3 -c "
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('$SOCKET')
s.send(b'$REQ\n')
s.shutdown(socket.SHUT_WR)
resp = b''
while True:
    d = s.recv(4096)
    if not d: break
    resp += d
s.close()
sys.stdout.write(resp.decode('utf-8', errors='replace'))
" 2>/dev/null || echo "查询失败"
