#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_NAME="can0.service"
APP_BINARY="$REPO_DIR/build/bmw-e46-dash"

echo "=== Installing dependencies ==="
sudo apt install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-serialbus-dev \
    can-utils \
    libsocketcan2 \
    xvfb

echo "=== Building application ==="
mkdir -p "$REPO_DIR/build"
cd "$REPO_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "=== Installing systemd CAN service ==="
sudo cp "$REPO_DIR/systemd/$SERVICE_NAME" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"

echo "=== Done ==="
echo "Plug in the USB2CANFD adapter and reboot, or run:"
echo "  sudo systemctl start $SERVICE_NAME"
echo "Then launch the dashboard with:"
echo "  DISPLAY= $APP_BINARY -platform offscreen"
