#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
DASHBOARD_USER="${SUDO_USER:-$(whoami)}"
CAN_SERVICE="can0.service"
DASH_SERVICE="bmw-e46-dash.service"

echo "=== Installing dependencies ==="
sudo apt install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-serialbus-dev \
    qt6-connectivity-dev \
    can-utils \
    libsocketcan2 \
    libegl-mesa0 \
    libgl1-mesa-dri

echo "=== Building application ==="
mkdir -p "$REPO_DIR/build"
cd "$REPO_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "=== Installing systemd services ==="
# CAN interface service (unmodified)
sudo cp "$REPO_DIR/systemd/$CAN_SERVICE" /etc/systemd/system/

# Dashboard service — substitute install path and user
sed "s|@INSTALL_DIR@|$REPO_DIR|g; s|@DASHBOARD_USER@|$DASHBOARD_USER|g" \
    "$REPO_DIR/systemd/$DASH_SERVICE" \
    | sudo tee /etc/systemd/system/$DASH_SERVICE > /dev/null

sudo systemctl daemon-reload
sudo systemctl enable "$CAN_SERVICE"
sudo systemctl enable "$DASH_SERVICE"

echo ""
echo "=== Done ==="
echo "User: $DASHBOARD_USER"
echo "Install dir: $REPO_DIR"
echo ""
echo "Reboot to start the dashboard automatically, or run:"
echo "  sudo systemctl start $CAN_SERVICE"
echo "  sudo systemctl start $DASH_SERVICE"
