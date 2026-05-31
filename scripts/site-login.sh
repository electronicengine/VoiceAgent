#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
DISPLAY_NUM=${DISPLAY_NUM:-99}
NOVNC_PORT=${NOVNC_PORT:-6080}
KEEP_NOVNC=${KEEP_NOVNC:-0}
NOVNC_PUBLIC=${NOVNC_PUBLIC:-1}
STARTED_NOVNC=0

SESSION_ID=""
DISPLAY_NAME=""
LOGIN_URL=""
LOGGED_IN_URL=""
LOGIN_CHECK_SELECTOR=""

usage() {
    cat <<EOF
Kullanim:
  scripts/site-login.sh --session-id <id> --display-name <ad> --login-url <url> --logged-in-url <url> [--login-check-selector <selector>]

Ornek:
  scripts/site-login.sh \
    --session-id vapi_main \
    --display-name "Vapi.ai" \
    --login-url "https://dashboard.vapi.ai/sign-in" \
    --logged-in-url "https://dashboard.vapi.ai/"

Opsiyonel ortam degiskenleri:
  DISPLAY_NUM=99
  NOVNC_PORT=6080
    NOVNC_PUBLIC=1
  KEEP_NOVNC=1
EOF
}

cleanup() {
    if [[ "${STARTED_NOVNC}" == "1" && "${KEEP_NOVNC}" != "1" ]]; then
        echo
        echo "[site-login] noVNC kapatiliyor..."
        bash "${REPO_ROOT}/scripts/novnc-down.sh" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

while [[ $# -gt 0 ]]; do
    case "$1" in
        --session-id)
            SESSION_ID=${2:-}
            shift 2
            ;;
        --display-name)
            DISPLAY_NAME=${2:-}
            shift 2
            ;;
        --login-url)
            LOGIN_URL=${2:-}
            shift 2
            ;;
        --logged-in-url)
            LOGGED_IN_URL=${2:-}
            shift 2
            ;;
        --login-check-selector)
            LOGIN_CHECK_SELECTOR=${2:-}
            shift 2
            ;;
        -h|--help)
            usage
            exit 2
            ;;
        *)
            echo "[site-login] Bilinmeyen arguman: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "${SESSION_ID}" || -z "${LOGIN_URL}" || -z "${LOGGED_IN_URL}" ]]; then
    usage
    exit 2
fi

if [[ -z "${DISPLAY_NAME}" ]]; then
    DISPLAY_NAME="${SESSION_ID}"
fi

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "[site-login] DISPLAY yok; noVNC sanal masaustu baslatiliyor..."
    DISPLAY_NUM="${DISPLAY_NUM}" NOVNC_PORT="${NOVNC_PORT}" NOVNC_PUBLIC="${NOVNC_PUBLIC}" bash "${REPO_ROOT}/scripts/novnc-up.sh"
    export DISPLAY=":${DISPLAY_NUM}"
    STARTED_NOVNC=1

    ROBOT_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
    ROBOT_IP=${ROBOT_IP:-<robot-ip>}
    echo
    if [[ "${NOVNC_PUBLIC}" == "1" ]]; then
        echo "[site-login] Tarayicida dogrudan ac:"
        echo "  http://${ROBOT_IP}:${NOVNC_PORT}/vnc.html"
    else
        echo "[site-login] Laptop'tan baglanmak icin ayri terminalde:"
        echo "  ssh -L ${NOVNC_PORT}:localhost:${NOVNC_PORT} ${USER}@${ROBOT_IP}"
        echo
        echo "[site-login] Sonra laptop tarayicisinda:"
        echo "  http://localhost:${NOVNC_PORT}/vnc.html"
    fi
    echo
fi

PYTHON_EXE="${REPO_ROOT}/.venv/bin/python3"
if [[ ! -x "${PYTHON_EXE}" ]]; then
    PYTHON_EXE=$(command -v python3)
fi

cd "${REPO_ROOT}"
cmd=(
    "${PYTHON_EXE}" "${REPO_ROOT}/scripts/site_login.py"
    --session-id "${SESSION_ID}"
    --display-name "${DISPLAY_NAME}"
    --login-url "${LOGIN_URL}"
    --logged-in-url "${LOGGED_IN_URL}"
)

if [[ -n "${LOGIN_CHECK_SELECTOR}" ]]; then
    cmd+=(--login-check-selector "${LOGIN_CHECK_SELECTOR}")
fi

"${cmd[@]}"
