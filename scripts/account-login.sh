#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
ACCOUNT_ID=${1:-}
DISPLAY_NUM=${DISPLAY_NUM:-99}
NOVNC_PORT=${NOVNC_PORT:-6080}
KEEP_NOVNC=${KEEP_NOVNC:-0}
NOVNC_PUBLIC=${NOVNC_PUBLIC:-1}
STARTED_NOVNC=0

usage() {
    cat <<EOF
Kullanim: scripts/account-login.sh <accountId>

Bu komut, account.json icindeki HERHANGI bir hesap kimligi ile calisir.
Yani google_main, linkedin_main, twitter_main, github_work gibi yeni eklenen
hesaplar ayni onboarding akisini kullanir.

Ornekler:
  scripts/account-login.sh google_main
  scripts/account-login.sh linkedin_main
    scripts/account-login.sh twitter_main

Opsiyonel ortam degiskenleri:
  DISPLAY_NUM=99
  NOVNC_PORT=6080
    NOVNC_PUBLIC=1   # noVNC'yi ayni agda dogrudan yayinla (varsayilan)
  KEEP_NOVNC=1    # script bitince noVNC'yi kapatma
EOF
}

cleanup() {
    if [[ "${STARTED_NOVNC}" == "1" && "${KEEP_NOVNC}" != "1" ]]; then
        echo
        echo "[account-login] noVNC kapatiliyor..."
        bash "${REPO_ROOT}/scripts/novnc-down.sh" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

if [[ -z "${ACCOUNT_ID}" || "${ACCOUNT_ID}" == "-h" || "${ACCOUNT_ID}" == "--help" ]]; then
    usage
    exit 2
fi

if [[ ! -f "${REPO_ROOT}/account.json" ]]; then
    echo "[account-login] account.json bulunamadi: ${REPO_ROOT}/account.json" >&2
    exit 1
fi

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "[account-login] DISPLAY yok; noVNC sanal masaustu baslatiliyor..."
    DISPLAY_NUM="${DISPLAY_NUM}" NOVNC_PORT="${NOVNC_PORT}" NOVNC_PUBLIC="${NOVNC_PUBLIC}" bash "${REPO_ROOT}/scripts/novnc-up.sh"
    export DISPLAY=":${DISPLAY_NUM}"
    STARTED_NOVNC=1

    ROBOT_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
    ROBOT_IP=${ROBOT_IP:-<robot-ip>}
    echo
    if [[ "${NOVNC_PUBLIC}" == "1" ]]; then
        echo "[account-login] Tarayicida dogrudan ac:"
        echo "  http://${ROBOT_IP}:${NOVNC_PORT}/vnc.html"
    else
        echo "[account-login] Laptop'tan baglanmak icin ayri terminalde:"
        echo "  ssh -L ${NOVNC_PORT}:localhost:${NOVNC_PORT} ${USER}@${ROBOT_IP}"
        echo
        echo "[account-login] Sonra laptop tarayicisinda:"
        echo "  http://localhost:${NOVNC_PORT}/vnc.html"
    fi
    echo
fi

PYTHON_EXE="${REPO_ROOT}/.venv/bin/python3"
if [[ ! -x "${PYTHON_EXE}" ]]; then
    PYTHON_EXE=$(command -v python3)
fi

cd "${REPO_ROOT}"
"${PYTHON_EXE}" "${REPO_ROOT}/scripts/account_login.py" "${ACCOUNT_ID}"
