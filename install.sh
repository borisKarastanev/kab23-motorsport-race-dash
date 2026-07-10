#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
DASHBOARD_USER="${SUDO_USER:-$(whoami)}"
USER_HOME=$(getent passwd "$DASHBOARD_USER" | cut -d: -f6)
CAN_SERVICE="can0.service"
DASH_SERVICE="bmw-e46-dash.service"

# Per-step timing: measure which "=== ... ===" section actually dominates
# install/update time instead of going on feel. Prints the previous section's
# elapsed time (via bash's builtin $SECONDS, 1s resolution — plenty for
# multi-second apt/plymouth steps) right before announcing the next one.
_step_start=$SECONDS
_step_name=""
step() {
    if [ -n "$_step_name" ]; then
        echo "    (${_step_name}: $((SECONDS - _step_start))s)"
    fi
    _step_name="$1"
    _step_start=$SECONDS
    echo "=== $1 ==="
}

step "Installing dependencies"
sudo apt install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-serialbus-dev \
    qt6-connectivity-dev \
    can-utils \
    libsocketcan2 \
    libxkbcommon-dev \
    libegl-mesa0 \
    libgl1-mesa-dri \
    ccache

step "Priming fontconfig cache"
# Qt builds the fontconfig cache on the first launch after a fresh image flash
# — a 1-3s one-time hit that would otherwise land on the dashboard's very
# first boot. The UI is all font.family: "monospace", so this is worth
# priming here instead of paying for it live.
fc-cache -f

step "Migrating legacy user data out of build/"
# The app now stores all persistent user data in a stable per-user directory
# (QStandardPaths AppDataLocation -> ~/.local/share/bmw-e46-dash, see
# src/apppaths.h) that no build step ever touches. Earlier versions stored it
# next to the binary — QCoreApplication::applicationDirPath() resolves to
# build/ — so a device updating from such a version still has irreplaceable race
# data sitting inside build/ that the wipe below would destroy. Move it into the
# stable directory ONCE, before the wipe.
#
# LEGACY_DATA_FILES is a fixed historical list on purpose: only pre-migration
# versions ever wrote to build/, so this set can never grow. Any file added in
# future is written straight to the stable directory and needs no migration.
# This MUST stay in sync with the AppDataLocation path baked into the binary via
# setApplicationName("bmw-e46-dash") in main.cpp.
DATA_DIR="$USER_HOME/.local/share/bmw-e46-dash"
LEGACY_DATA_FILES=(sessions.json tracks-user.json dashboard.conf track-db.json dash.log dash.log.1)
if [ -d "$REPO_DIR/build" ]; then
    sudo -u "$DASHBOARD_USER" mkdir -p "$DATA_DIR"
    for f in "${LEGACY_DATA_FILES[@]}"; do
        # Only migrate when the stable copy doesn't already exist, so a stale
        # build/ leftover can never clobber newer data already in the stable dir.
        if [ -f "$REPO_DIR/build/$f" ] && [ ! -e "$DATA_DIR/$f" ]; then
            sudo -u "$DASHBOARD_USER" cp -p "$REPO_DIR/build/$f" "$DATA_DIR/$f"
            echo "    (migrated $f -> $DATA_DIR)"
        fi
    done
fi

step "Building application"
# Wipe the build directory rather than reusing it: CMake's Unix Makefiles
# generator regenerates shared per-target bookkeeping on any CMakeLists.txt
# edit, so an incremental build here has previously linked in stale
# generated resource/object files from a prior build-system config (e.g.
# the qt_add_qml_module revert) alongside the current source, producing a
# binary with mismatched/duplicate QML resource paths that crashes at
# startup. ccache (below) makes a full rebuild cheap, so there's no
# incremental-build fragility left to trade for the risk. User data no longer
# lives here (see the migration step above), so the wipe is non-destructive.
# Removing (not just unlinking the binary) is still ETXTBSY-safe if this
# runs as an in-app update while the dashboard is running: the running
# process keeps its inode open until it exits, same as a plain rm -f would.
rm -rf "$REPO_DIR/build"
mkdir -p "$REPO_DIR/build"
chown -R "$DASHBOARD_USER:$DASHBOARD_USER" "$REPO_DIR/build"
# Run cmake and make as the repo owner — Qt cmake macros write into the source
# tree via configure_file, which fails when running as root.
sudo -u "$DASHBOARD_USER" bash -c "cd '$REPO_DIR/build' && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j\$(nproc)"

step "Enabling NetworkManager control for the dashboard user"
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

step "Granting backlight brightness control"
# The Display settings page (DisplayModel) writes
# /sys/class/backlight/*/brightness directly. That node is normally
# root-owned; on a desktop session a logind/udev rule usually makes it
# group-writable, but this service has no active logind session (same
# console-boot gap as the NetworkManager rule above), so nothing grants
# access by default. The service already runs with SupplementaryGroups=video
# (see bmw-e46-dash.service) — this rule just makes the node writable by that
# group, so no service-file change is needed. Idempotent: udev rules are
# just files, safe to overwrite on every install/update.
sudo mkdir -p /etc/udev/rules.d
sudo tee /etc/udev/rules.d/99-race-dash-backlight.rules > /dev/null << 'RULES'
SUBSYSTEM=="backlight", ACTION=="add", RUN+="/bin/chgrp video /sys/class/backlight/%k/brightness", RUN+="/bin/chmod g+w /sys/class/backlight/%k/brightness"
RULES
sudo udevadm control --reload
# --action=add is required: the rule matches ACTION=="add", but `udevadm
# trigger` defaults to emitting a "change" event, which would NOT match and so
# would leave the node non-writable until the next real add (i.e. a reboot).
# Forcing an add re-runs the rule now, on the already-present device.
sudo udevadm trigger --action=add --subsystem-match=backlight

step "Enabling Bluetooth adapter"
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

step "Enabling persistent journald logging"
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

step "Installing systemd CAN service"
sudo cp "$REPO_DIR/systemd/$CAN_SERVICE" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable "$CAN_SERVICE"
# Note: 'non-existent unit dev-ttyACM0.device' warning above is expected —
# that unit is created by udev only when the USB2CAN adapter is plugged in.

step "Installing dashboard system service (console/eglfs mode)"
sed "s|@INSTALL_DIR@|$REPO_DIR|g; s|@DASHBOARD_USER@|$DASHBOARD_USER|g" \
    "$REPO_DIR/systemd/$DASH_SERVICE" \
    | sudo tee /etc/systemd/system/$DASH_SERVICE > /dev/null
sudo systemctl daemon-reload
sudo systemctl enable "$DASH_SERVICE"

step "Configuring clean console boot (kiosk display)"
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

step "Installing Plymouth boot splash"
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
#
# -R regenerates initramfs for every kernel flavor under /boot (on this
# image, both rpi-v8 and rpi-2712) — tried scoping that to just the running
# kernel via `-k "$(uname -r)"`, but Raspberry Pi OS's raspi-firmware hooks
# regenerate both regardless (that's by design: it's what lets the same SD
# card boot on Pi 3/4 or Pi 5/CM5), so the split bought nothing and just
# added complexity. Back to plain -R.
sudo apt install -y plymouth plymouth-themes
sudo rm -rf /usr/share/plymouth/themes/race-dash
sudo cp -r "$REPO_DIR/plymouth/race-dash" /usr/share/plymouth/themes/race-dash
# Tolerate failure of this step (under `set -e`): -R regenerates the initramfs,
# which can fail on setups where the update-initramfs / raspi-firmware hook is
# unhappy. A missing splash is cosmetic — don't abort the rest of provisioning
# (systemd units, autostart) over it.
sudo plymouth-set-default-theme -R race-dash \
    || echo "WARNING: plymouth-set-default-theme failed (initramfs regen?); boot splash may not appear — continuing install."

step "Trimming boot-irrelevant units"
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

step "Installing XDG autostart (desktop/Wayland/X11 mode)"
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
step "Done"
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
echo ""
echo "    (${_step_name}: $((SECONDS - _step_start))s)"
echo "Total install.sh time: ${SECONDS}s"
