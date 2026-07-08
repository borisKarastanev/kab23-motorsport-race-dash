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

echo "=== Priming fontconfig cache ==="
# Qt builds the fontconfig cache on the first launch after a fresh image flash
# — a 1-3s one-time hit that would otherwise land on the dashboard's very
# first boot. The UI is all font.family: "monospace", so this is worth
# priming here instead of paying for it live.
fc-cache -f

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

echo "=== Configuring clean console boot (kiosk display) ==="
# The dashboard drives the DSI panel directly through Qt eglfs (bare KMS), so it
# must be the sole DRM master. Two things otherwise fight it for the screen:
#   1. The graphical desktop (labwc/Wayland). If the Pi boots to desktop, the
#      compositor holds DRM master and every eglfs page flip is denied
#      ("Could not queue DRM page flip ... Permission denied") — a frame-rate
#      log flood that also leaves the dashboard invisible.
#   2. The tty1 framebuffer console + login prompt, visible on the panel from
#      power-on until the service draws its first frame — a raw Linux login
#      prompt is not something dashboard users should ever see.
# Force console boot and hide the console so the panel shows black -> dashboard.
# Every step here is idempotent (this script also runs as the in-app updater).

# Boot to console, never graphical.target.
sudo systemctl set-default multi-user.target

# Disable the tty1 autologin: it both shows a login prompt on the panel and, on
# Pi OS, is what launches the labwc desktop session. Other VTs are left alone so
# Ctrl+Alt+F2 remains an emergency console.
sudo systemctl disable --now getty@tty1 2>/dev/null || true
sudo rm -f /etc/systemd/system/getty@tty1.service.d/autologin.conf

# Kernel cmdline + firmware splash. Bookworm moved these under /boot/firmware.
BOOT_DIR=/boot/firmware
[ -d "$BOOT_DIR" ] || BOOT_DIR=/boot
CMDLINE="$BOOT_DIR/cmdline.txt"
CONFIG="$BOOT_DIR/config.txt"

if [ -f "$CMDLINE" ]; then
    # Move the framebuffer console off the panel (tty1 -> tty3) and quiet the
    # boot so no kernel text is drawn on screen.
    sudo sed -i 's/\bconsole=tty1\b/console=tty3/g' "$CMDLINE"
    # "splash" is what tells Plymouth (installed below) to actually draw its
    # graphical splash instead of running invisibly.
    for tok in quiet splash loglevel=3 logo.nologo vt.global_cursor_default=0; do
        sudo grep -qw -- "$tok" "$CMDLINE" || sudo sed -i "1 s|\$| $tok|" "$CMDLINE"
    done
fi

if [ -f "$CONFIG" ]; then
    # Suppress the rainbow splash the GPU draws before the kernel starts, and
    # skip the firmware's default boot-settling delay. Managed as a marked
    # block under [all] — an unconditional filter appended last, so it
    # overrides any earlier board-specific ([pi4]/[cm4]/…) or pre-existing
    # disable_splash/boot_delay setting regardless of where the file ends.
    # Removed and re-appended each run, so repeated installs/updates stay
    # idempotent instead of accumulating duplicate keys.
    #
    # arm_boost=1 (Pi 4 turbo clock during boot) is intentionally NOT set
    # here yet — it needs confirming this board is actually a Pi 4 (vs. Pi 3)
    # before it's safe to bake in; see boot-time-optimization.md Step 0.
    sudo sed -i '/^# --- race-dash boot (managed) ---$/,/^# --- end race-dash boot ---$/d' "$CONFIG"
    sudo tee -a "$CONFIG" > /dev/null << 'BOOTCFG'
# --- race-dash boot (managed) ---
[all]
disable_splash=1
boot_delay=0
# --- end race-dash boot ---
BOOTCFG
fi

echo "=== Installing Plymouth boot splash ==="
# This is a separate boot stage from disable_splash above (that's the GPU
# firmware's rainbow test-card, shown before the kernel even starts; Plymouth
# runs from the initramfs right after). Ships our own small theme
# (plymouth/race-dash/ — a "LOADING..." animation matching the dashboard's
# dark HUD look) instead of a stock one, so the panel goes straight from black
# to that to the running dashboard, with no console text or login prompt
# in between (see the console-boot section above for those).
#
# The dashboard's own systemd unit (Type=notify) is what makes the handoff
# race-free: it signals readiness only after its first QML frame is actually
# on screen (see main.cpp / sdnotify.cpp), and only then does
# ExecStartPost=plymouth quit in bmw-e46-dash.service dismiss the splash — so
# it can't be dismissed early and leave a black gap before the dashboard.
# ExecStartPre=plymouth deactivate in that same unit releases the DRM master
# before the app starts (leaving the splash's last frame on screen) so eglfs
# never has to fight Plymouth for it.
#
# Mask the stock quit units so nothing else can dismiss the splash before our
# unit does: plymouth-quit.service normally fires at multi-user.target,
# independent of whether the dashboard has actually painted yet, which would
# defeat the READY-gated handoff above (splash gone, dashboard not there yet
# -> black gap). plymouth-quit-wait.service is masked alongside it since
# nothing legitimately needs to block on the splash quitting (getty@tty1,
# the only such consumer on a stock image, is already disabled above).
sudo systemctl mask plymouth-quit.service plymouth-quit-wait.service
#
# plymouth-set-default-theme -R both selects the theme and regenerates the
# initramfs so it's baked into the next boot; on Raspberry Pi OS this also
# updates config.txt's `initramfs` line automatically via the raspi-firmware
# package hooks. plymouth-themes is a fallback in case this custom theme ever
# fails to install correctly.
sudo apt install -y plymouth plymouth-themes
sudo rm -rf /usr/share/plymouth/themes/race-dash
sudo cp -r "$REPO_DIR/plymouth/race-dash" /usr/share/plymouth/themes/race-dash
# Tolerate failure of this step (under `set -e`): -R regenerates the initramfs,
# which can fail on setups where the update-initramfs / raspi-firmware hook is
# unhappy. A missing splash is cosmetic — don't abort the rest of provisioning
# (systemd units, autostart) over it.
sudo plymouth-set-default-theme -R race-dash \
    || echo "WARNING: plymouth-set-default-theme failed (initramfs regen?); boot splash may not appear — continuing install."

echo "=== Trimming boot-irrelevant units ==="
# This is a dedicated car dashboard, not a general-purpose desktop — these
# units cost boot time for capabilities the device never uses. Each is
# `disable --now`, not `mask`, so it's trivially reversible, and each
# tolerates the unit being absent (varies by image) without aborting the
# rest of provisioning under `set -e`.
#
# NetworkManager-wait-online.service is a boot GATE that blocks
# network-online.target until a connection is confirmed — it does not bring
# up networking itself (that still happens, just asynchronously). The
# dashboard's NetworkModel polls nmcli on its own timer and has no
# network-online.target ordering, so nothing here needs it.
sudo systemctl disable --now NetworkManager-wait-online.service 2>/dev/null || true
sudo systemctl disable --now systemd-networkd-wait-online.service 2>/dev/null || true
# apt/man-db background timers, the cellular modem manager, the triggerhappy
# hotkey daemon, and the SD-card swapfile service are all dead weight on a
# device with no keyboard shortcuts, no modem, and (ideally) no swap thrash
# against the SD card.
sudo systemctl disable --now apt-daily.timer apt-daily-upgrade.timer man-db.timer 2>/dev/null || true
sudo systemctl disable --now ModemManager.service triggerhappy.service 2>/dev/null || true
sudo systemctl disable --now dphys-swapfile.service rpi-eeprom-update.service 2>/dev/null || true

echo "=== Installing XDG autostart (desktop/Wayland/X11 mode) ==="
# NOTE: console boot is enforced above, so this desktop-session autostart is
# inert on a normally-provisioned device. It only takes effect if someone later
# re-enables the desktop (systemctl set-default graphical.target) — in which
# case the eglfs system service MUST be disabled first, or the two will fight
# for DRM master (see the console-boot section above).
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
echo "Provisioned for console boot: the systemd service drives the panel via"
echo "eglfs, and the desktop/console are suppressed so users see only a"
echo "Plymouth \"LOADING...\" splash, then the dashboard — no login prompt,"
echo "no boot text, no unindicated blank screen."
echo ""
echo "*** A REBOOT is required for the boot-display changes to take effect. ***"
echo ""
echo "To start manually now:"
echo "  $REPO_DIR/build/bmw-e46-dash --kiosk"
echo ""
echo "To view dashboard logs (persisted across reboots):"
echo "  journalctl -u bmw-e46-dash"
echo "  journalctl -u bmw-e46-dash --since '2025-06-18 22:00'"
