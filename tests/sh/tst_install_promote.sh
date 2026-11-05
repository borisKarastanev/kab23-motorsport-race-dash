#!/bin/bash
# install.sh's binary deployment: what the next boot runs (bin/) is only ever
# replaced by a complete build, so a failed or interrupted update keeps booting
# the previous one (see install.sh's deploy helpers).
#
# Usage: tst_install_promote.sh <repo-root>
# Sources only the marked "deploy helpers" block of install.sh, so nothing
# privileged (apt, systemctl, sudo) ever runs.

set -u

REPO="${1:?usage: $0 <repo-root>}"
INSTALL_SH="$REPO/install.sh"
SERVICE="$REPO/systemd/bmw-e46-dash.service"

failures=0
pass()   { echo "PASS: $1"; }
fail()   { echo "FAIL: $1"; failures=$((failures + 1)); }
check()  { local d="$1"; shift; if "$@"; then pass "$d"; else fail "$d"; fi; }
refute() { local d="$1"; shift; if "$@" 2>/dev/null; then fail "$d"; else pass "$d"; fi; }
inode()  { ls -i "$1" | awk '{print $1}'; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

HELPERS="$WORK/helpers.sh"
sed -n '/^# --- deploy helpers/,/^# --- end deploy helpers ---$/p' "$INSTALL_SH" > "$HELPERS"
if ! grep -q '^promote_binary()' "$HELPERS"; then
    echo "FAIL: could not extract the deploy helpers block from install.sh"
    exit 1
fi

# Fresh fake repo; the helpers derive BUILD_BIN/DASH_BIN from these.
REPO_DIR="$WORK/repo"
DASHBOARD_USER=$(id -un)
# shellcheck source=/dev/null
source "$HELPERS"
mkdir -p "$BUILD_DIR"

fake_build() { # fake_build <content>
    printf '#!/bin/sh\necho %s\n' "$1" > "$BUILD_BIN"
    chmod 755 "$BUILD_BIN"
}

# Called from inside `if` (via check/refute), where `set -e` is inert — so these
# results prove promote_binary fails safely on its own, not thanks to set -e.
promote() { promote_binary "$BUILD_BIN"; }

content_is() { [ "$(sed -n 2p "$1" 2>/dev/null)" = "echo $2" ]; }
mode_of()    { stat -c %a "$1" 2>/dev/null || stat -f %Lp "$1"; }

# ── first install: bin/ created, binary executable, no .prev ─────────
fake_build v1
check  "first promote succeeds"                    promote
check  "bin/bmw-e46-dash holds the build"          content_is "$DASH_BIN" v1
check  "bin/bmw-e46-dash is executable"            test -x "$DASH_BIN"
check  "no .prev on first install"                 test ! -e "$DASH_BIN.prev"

# ── update: new binary live by rename, previous kept as .prev ───────
old_inode=$(inode "$DASH_BIN")
fake_build v2
check  "update promote succeeds"                   promote
check  "bin/bmw-e46-dash is the new build"         content_is "$DASH_BIN" v2
check  ".prev is the previous build"               content_is "$DASH_BIN.prev" v1
check  "no leftover temp file"                     test ! -e "$BIN_DIR/.bmw-e46-dash.new"
check  "swapped in as a new inode (ETXTBSY-safe)"  test "$old_inode" != "$(inode "$DASH_BIN")"
check  ".prev is the old inode, not a copy"        test "$old_inode" = "$(inode "$DASH_BIN.prev")"

# ── same build re-run: .prev keeps the real fallback, mode repaired ─
chmod 700 "$DASH_BIN"
check  "identical re-run succeeds"                 promote
check  ".prev still the older build after re-run"  content_is "$DASH_BIN.prev" v1
check  "identical re-run repairs mode to 755"      test "$(mode_of "$DASH_BIN")" = 755

# ── failed build: no binary, or not executable -> bin/ untouched ────
rm -rf "$BUILD_DIR" && mkdir -p "$BUILD_DIR"
refute "promote with no built binary fails"        promote
fake_build v3
chmod 644 "$BUILD_BIN"
refute "promote of a non-executable build fails"   promote
check  "bin/bmw-e46-dash survives failed builds"   content_is "$DASH_BIN" v2
check  "bin/bmw-e46-dash still executable"         test -x "$DASH_BIN"
check  ".prev survives failed builds"              content_is "$DASH_BIN.prev" v1

# ── wiring: the build tree is never what the next boot runs ─────────
body=$(sed '/^# --- deploy helpers/,/^# --- end deploy helpers ---$/d' "$INSTALL_SH")
activate_line=$(printf '%s\n' "$body" | grep -n '^step "Activating new build"' | cut -d: -f1)
first_unit_install=$(printf '%s\n' "$body" | grep -n '^[[:space:]]*install_dash_service' | head -1 | cut -d: -f1)
check  "new unit installed only in the final step" test -n "$activate_line" -a "${first_unit_install:-0}" -gt "${activate_line:-0}"
check  "only BUILD_DIR is wiped"                   grep -q '^rm -rf "\$BUILD_DIR"$' "$INSTALL_SH"
refute "the deployed binary's dir is never wiped"  grep -qE 'rm -rf "?\$(REPO_DIR/bin|BIN_DIR)' "$INSTALL_SH"
check  "service ExecStart launches bin/"           grep -q '^ExecStart=@INSTALL_DIR@/bin/bmw-e46-dash' "$SERVICE"
check  "XDG autostart launches \$DASH_BIN"         grep -q '^Exec=\$DASH_BIN --kiosk' "$INSTALL_SH"

echo
if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
