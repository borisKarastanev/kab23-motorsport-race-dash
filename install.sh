#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
DASHBOARD_USER="${SUDO_USER:-$(whoami)}"
USER_HOME=$(getent passwd "$DASHBOARD_USER" | cut -d: -f6)
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
    libxkbcommon-dev \
    libegl-mesa0 \
    libgl1-mesa-dri

echo "=== Building application ==="
mkdir -p "$REPO_DIR/build"
cd "$REPO_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "=== Installing systemd CAN service ==="
sudo cp "$REPO_DIR/systemd/$CAN_SERVICE" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable "$CAN_SERVICE"
# Note: 'non-existent unit dev-ttyACM0.device' warning above is expected —
# that unit is created by udev only when the USB2CAN adapter is plugged in.

echo "=== Installing dashboard system service (console/eglfs mode) ==="
sed "s|@INSTALL_DIR@|$REPO_DIR|g; s|@DASHBOARD_USER@|$DASHBOARD_USER|g" \
    "$REPO_DIR/systemd/$DASH_SERVICE" \
    | sudo tee /etc/systemd/system/$DASH_SERVICE > /dev/null
sudo systemctl daemon-reload
sudo systemctl enable "$DASH_SERVICE"

echo "=== Installing XDG autostart (desktop/Wayland/X11 mode) ==="
AUTOSTART_DIR="$USER_HOME/.config/autostart"
mkdir -p "$AUTOSTART_DIR"
cat > "$AUTOSTART_DIR/bmw-e46-dash.desktop" << EOF
[Desktop Entry]
Type=Application
Name=BMW E46 Race Dashboard
Exec=$REPO_DIR/build/bmw-e46-dash --kiosk
EOF
chown -R "$DASHBOARD_USER:$DASHBOARD_USER" "$AUTOSTART_DIR"
echo "Autostart file: $AUTOSTART_DIR/bmw-e46-dash.desktop"

echo ""
echo "=== Done ==="
echo "User: $DASHBOARD_USER  |  Install dir: $REPO_DIR"
echo ""
echo "On next boot the dashboard starts automatically."
echo "Desktop session (Wayland/X11): XDG autostart launches the app."
echo "Console/CLI mode:              systemd service uses eglfs."
echo ""
echo "To start manually now:"
echo "  $REPO_DIR/build/bmw-e46-dash --kiosk"
