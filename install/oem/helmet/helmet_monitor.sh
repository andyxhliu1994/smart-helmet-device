#!/bin/sh

PROCESS_NAME="helmet"
PROCESS_PATH="/oem/helmet/helmet"
PROCESS_ARGS="-c /oem/helmet/test.json"
LOG_FILE="/tmp/helmet_monitor.log"
PID_FILE="/tmp/helmet.pid"
CHECK_INTERVAL=5

log_msg() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> "$LOG_FILE"
}

# 检查进程是否在运行（使用 PID 文件）
check_process() {
    pidof "$PROCESS_NAME" >/dev/null 2>&1
}

# 启动进程并保存 PID
start_process() {
    log_msg "进程 $PROCESS_NAME 未运行，正在启动..."
    
    if [ -x "$PROCESS_PATH" ]; then
        LD_LIBRARY_PATH="/oem/helmet/lib:${LD_LIBRARY_PATH}" nohup "$PROCESS_PATH" $PROCESS_ARGS >> "$LOG_FILE" 2>&1 </dev/null &
        local pid=$!
        echo "$pid" > "$PID_FILE"
        log_msg "进程 $PROCESS_NAME 已启动 (PID: $pid)"
    else
        log_msg "错误: $PROCESS_PATH 不存在或不可执行"
    fi
}

main() {
    log_msg "=== helmet 进程监测脚本启动 ==="
    while true; do
        if ! check_process; then
            start_process
        fi
        sleep $CHECK_INTERVAL
    done
}

trap 'log_msg "监测脚本退出"; rm -f "$PID_FILE"; exit 0' INT TERM

main
