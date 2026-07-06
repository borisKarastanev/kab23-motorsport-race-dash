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
chown -R "$DASHBOARD_USER:$DASHBOARD_USER" "$REPO_DIR/build"
# Unlink the previous binary before relinking — if this script is run as an
# in-app update while the dashboard is still running, ld writing the output
# in place would hit ETXTBSY. Unlinking is safe: the running process keeps
# its inode open until it exits.
rm -f "$REPO_DIR/build/bmw-e46-dash"
# Run cmake and make as the repo owner — Qt cmake macros write into the source
# tree via configure_file, which fails when running as root.
sudo -u "$DASHBOARD_USER" bash -c "cd '$REPO_DIR/build' && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j\$(nproc)"

echo "=== Enabling NetworkManager control for the dashboard user ==="
# Network Connection settings shell out to nmcli as the dashboard user (no
# sudo). netdev group membership is what nmcli's polkit policy normally
# checks, but the dashboard runs as a systemd service with no active logind
# session, so the stock "active session" polkit rule doesn't apply here —
# without this explicit rule, nmcli calls are silently denied under the
# service even though they work fine when tested from an interactive shell.
sudo usermod -aG netdev "$DASHBOARD_USER"
sudo mkdir -p /etc/polkit-1/rules.d
sudo tee /etc/polkit-1/rules.d/50-race-dash-nm.rules > /dev/null << 'RULES'
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.isInGroup("netdev")) {
        return polkit.Result.YES;
    }
});
RULES

echo "=== Enabling Bluetooth adapter ==="
rfkill unblock bluetooth || true
# Drop-in for bluetooth.service: unblock rfkill just before bluetoothd starts.
# More reliable than a standalone service — runs at exactly the right time.
sudo mkdir -p /etc/systemd/system/bluetooth.service.d
sudo tee /etc/systemd/system/bluetooth.service.d/unblock-rfkill.conf > /dev/null << 'DROPIN'
[Service]
ExecStartPre=-/usr/sbin/rfkill unblock bluetooth
DROPIN
# Remove the old standalone service if present from a previous install
sudo systemctl disable bluetooth-unblock.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/bluetooth-unblock.service
sudo systemctl daemon-reload
sudo systemctl restart bluetooth.service || true

echo "=== Enabling persistent journald logging ==="
# By default Raspberry Pi OS uses volatile storage (/run/log/journal) — logs
# are lost on power-off. Creating /var/log/journal makes journald switch to
# persistent mode (Storage=auto picks it up without editing journald.conf).
sudo mkdir -p /var/log/journal
sudo systemd-tmpfiles --create --prefix /var/log/journal
# Cap total log size so it doesn't fill the SD card over time.
JOURNALD_CONF=/etc/systemd/journald.conf.d/dashboard.conf
sudo mkdir -p /etc/systemd/journald.conf.d
sudo tee "$JOURNALD_CONF" > /dev/null << 'JOURNALD'
[Journal]
SystemMaxUse=100M
MaxRetentionSec=2week
JOURNALD
sudo systemctl restart systemd-journald

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
echo ""
echo "To view dashboard logs (persisted across reboots):"
echo "  journalctl -u bmw-e46-dash"
echo "  journalctl -u bmw-e46-dash --since '2025-06-18 22:00'"
