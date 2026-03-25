#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="deskclock-kiosk.service"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SERVICE_SRC="$REPO_DIR/scripts/raspi/$SERVICE_NAME"
SERVICE_DST="/etc/systemd/system/$SERVICE_NAME"

if [[ "$EUID" -ne 0 ]]; then
  echo "Run as root: sudo $0"
  exit 1
fi

if [[ ! -f "$SERVICE_SRC" ]]; then
  echo "Missing service file: $SERVICE_SRC"
  exit 1
fi

install -m 0644 "$SERVICE_SRC" "$SERVICE_DST"

# Keep scripts executable after clone/update.
chmod +x "$REPO_DIR/scripts/raspi/start_kiosk.sh"

systemctl daemon-reload
systemctl enable "$SERVICE_NAME"
systemctl restart "$SERVICE_NAME"

systemctl --no-pager --full status "$SERVICE_NAME" || true

echo
echo "Installed and started $SERVICE_NAME"
echo "Live logs: journalctl -u $SERVICE_NAME -f"
