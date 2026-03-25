#!/usr/bin/env bash
set -euo pipefail

APP_DIR="${APP_DIR:-/home/sdgp/UI/Deskclock-UI}"
APP_BIN="${APP_BIN:-./lvgl_app}"
CAMERA_WRAPPER="${CAMERA_WRAPPER:-libcamerify}"

cd "$APP_DIR"

# Keep tty output visible in kiosk mode and disable power-saving blanking.
if command -v setterm >/dev/null 2>&1; then
  setterm -blank 0 -powersave off -powerdown 0 >/dev/tty1 || true
fi

exec "$CAMERA_WRAPPER" "$APP_BIN"
