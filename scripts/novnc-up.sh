#!/bin/bash
# Starts a virtual desktop (Xvfb + fluxbox + x11vnc + noVNC) on the robot.
# Default mode exposes noVNC on the local network so the user can open a
# single web page: http://<robot-ip>:6080/vnc.html
# Set NOVNC_PUBLIC=0 to keep the old localhost-only + SSH tunnel mode.

set -e

DISPLAY_NUM=${DISPLAY_NUM:-99}
SCREEN_GEOMETRY=${SCREEN_GEOMETRY:-1366x900x24}
VNC_PORT=${VNC_PORT:-5901}
NOVNC_PORT=${NOVNC_PORT:-6080}
NOVNC_HTML_DIR=${NOVNC_HTML_DIR:-/usr/share/novnc}
NOVNC_PUBLIC=${NOVNC_PUBLIC:-1}

if [[ "${NOVNC_PUBLIC}" == "1" ]]; then
  VNC_BIND_HOST="0.0.0.0"
  NOVNC_BIND_HOST="0.0.0.0"
else
  VNC_BIND_HOST="127.0.0.1"
  NOVNC_BIND_HOST="127.0.0.1"
fi

PID_DIR="${HOME}/.voice_agent_browser/novnc"
mkdir -p "${PID_DIR}"

ROBOT_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
ROBOT_IP=${ROBOT_IP:-<robot-ip>}

is_running() {
    local pidfile="$1"
    [[ -f "${pidfile}" ]] && kill -0 "$(cat "${pidfile}")" 2>/dev/null
}

start_proc() {
    local name="$1"; shift
    local pidfile="${PID_DIR}/${name}.pid"
    if is_running "${pidfile}"; then
        echo "[novnc-up] ${name} already running (pid $(cat "${pidfile}"))"
        return 0
    fi
    local logfile="${PID_DIR}/${name}.log"
    : > "${logfile}"
    nohup "$@" >>"${logfile}" 2>&1 &
    echo $! > "${pidfile}"
    sleep 0.4
    if ! is_running "${pidfile}"; then
        echo "[novnc-up] ${name} failed to start. Last log:"
        tail -20 "${logfile}"
        return 1
    fi
    echo "[novnc-up] ${name} started (pid $(cat "${pidfile}"))"
}

# 1) Xvfb virtual display
start_proc "xvfb" Xvfb ":${DISPLAY_NUM}" -screen 0 "${SCREEN_GEOMETRY}" -nolisten tcp -ac

# 2) Fluxbox window manager (so chromium has decorations / focus)
start_proc "fluxbox" env DISPLAY=":${DISPLAY_NUM}" fluxbox

# 3) x11vnc; in public mode it binds on the LAN, otherwise only localhost.
if [[ "${NOVNC_PUBLIC}" == "1" ]]; then
    start_proc "x11vnc" x11vnc -display ":${DISPLAY_NUM}" -rfbport "${VNC_PORT}" \
        -forever -shared -nopw -quiet
else
    start_proc "x11vnc" x11vnc -display ":${DISPLAY_NUM}" -rfbport "${VNC_PORT}" \
        -localhost -forever -shared -nopw -quiet
fi

# 4) websockify (noVNC bridge)
start_proc "websockify" websockify --web="${NOVNC_HTML_DIR}" \
    "${NOVNC_BIND_HOST}:${NOVNC_PORT}" "${VNC_BIND_HOST}:${VNC_PORT}"

if [[ "${NOVNC_PUBLIC}" == "1" ]]; then
cat <<EOF

[novnc-up] Sanal masaustu hazir:
  DISPLAY=:${DISPLAY_NUM}  (Xvfb, ${SCREEN_GEOMETRY})
  noVNC: http://${ROBOT_IP}:${NOVNC_PORT}/vnc.html  (aynı ağdan doğrudan erişim)

Laptop veya telefondan tarayıcıda aç:
  http://${ROBOT_IP}:${NOVNC_PORT}/vnc.html

Agent'i bu DISPLAY ile calistirmak icin (ayni shell veya yeni bir terminal):
  export DISPLAY=:${DISPLAY_NUM}
  cd build && ./cpp_voice_agent

Durdurmak icin:
  scripts/novnc-down.sh
EOF
else
cat <<EOF

[novnc-up] Sanal masaustu hazir:
  DISPLAY=:${DISPLAY_NUM}  (Xvfb, ${SCREEN_GEOMETRY})
  noVNC: http://localhost:${NOVNC_PORT}/vnc.html  (SSH tunnel uzerinden)

Laptop'tan baglanmak icin (ayri bir terminal):
  ssh -L ${NOVNC_PORT}:localhost:${NOVNC_PORT} ${USER}@${ROBOT_IP}

Sonra laptop tarayicisinda:
  http://localhost:${NOVNC_PORT}/vnc.html

Agent'i bu DISPLAY ile calistirmak icin (ayni shell veya yeni bir terminal):
  export DISPLAY=:${DISPLAY_NUM}
  cd build && ./cpp_voice_agent

Durdurmak icin:
  scripts/novnc-down.sh
EOF
fi
