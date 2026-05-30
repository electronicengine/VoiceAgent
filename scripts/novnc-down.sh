#!/bin/bash
# Stops the noVNC stack started by novnc-up.sh.

PID_DIR="${HOME}/.voice_agent_browser/novnc"
[[ -d "${PID_DIR}" ]] || { echo "[novnc-down] No PID dir, nothing to do."; exit 0; }

stop_proc() {
    local name="$1"
    local pidfile="${PID_DIR}/${name}.pid"
    if [[ ! -f "${pidfile}" ]]; then
        echo "[novnc-down] ${name}: no pidfile"
        return 0
    fi
    local pid
    pid=$(cat "${pidfile}")
    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null
        sleep 0.3
        kill -9 "${pid}" 2>/dev/null || true
        echo "[novnc-down] ${name}: stopped (pid ${pid})"
    else
        echo "[novnc-down] ${name}: not running"
    fi
    rm -f "${pidfile}"
}

stop_proc "websockify"
stop_proc "x11vnc"
# x11vnc may also have been started in -bg mode; pkill any stray processes for our display
pkill -f "x11vnc -display :${DISPLAY_NUM:-99}" 2>/dev/null || true
stop_proc "fluxbox"
stop_proc "xvfb"
echo "[novnc-down] Done."
