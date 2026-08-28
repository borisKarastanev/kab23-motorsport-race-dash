#pragma once

// Signals systemd readiness (READY=1) so a Type=notify unit's ExecStartPost
// (here: "plymouth quit") fires exactly once the first frame has been drawn,
// instead of racing the boot splash against app startup. Implements the
// sd_notify(3) wire protocol directly (a single datagram to $NOTIFY_SOCKET)
// rather than linking libsystemd for one call; a no-op, safe to call
// unconditionally, when not running under systemd (e.g. a local --mock run
// has no $NOTIFY_SOCKET).
void sdNotifyReady();
