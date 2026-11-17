#pragma once

#include <QObject>
#include <QTimer>

class CanDataModel;

// Idle auto-shutdown. Turning the key to position 0 cuts the Pi's power
// outright, which is hard on the SD card, so once the engine is off (key back
// to position 1) and nobody has touched the screen for kIdleMs, this shows a
// countdown and then powers the Pi off cleanly with `systemctl poweroff`.
// Any touch, click or key press restarts the idle wait. Once the countdown is
// showing, cancelling is the overlay's job (see eventFilter()).
//
// Armed only while the engine is stopped, the car is standing still and
// nothing has blocked it (an update in progress). "Stopped" and "standing"
// must be evidence, not silence: nothing arms before the first CAN frame (a
// missing or slow adapter at boot reads exactly like a stopped engine), and
// once the car has been seen moving it counts as moving until a trusted
// source — fresh MK60 wheel speeds, or GPS with a fix — reads standstill.
// Without that latch a CAN adapter dying mid-drive, with no RaceBox, would
// read as engine off at 0 km/h. While disarmed
// there is no event filter and no running timer, so a running engine costs
// nothing. Restarting the engine mid-countdown drops it.
//
// Stays decoupled from the models that need to flush on the way down:
// aboutToPowerOff() is emitted synchronously right before poweroff, and
// main.cpp connects the session save and the uplink's final `stop` to it.
// powerOffAborted() follows if the poweroff then fails (or in mock mode), so
// whatever aboutToPowerOff shut down can come back up.
class PowerModel : public QObject {
    Q_OBJECT
    // The countdown overlay is showing.
    Q_PROPERTY(bool shutdownPending READ shutdownPending NOTIFY shutdownPendingChanged)
    Q_PROPERTY(int secondsRemaining READ secondsRemaining NOTIFY secondsRemainingChanged)
    // The countdown ran out: saving and powering off, no longer cancellable.
    Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shuttingDownChanged)

public:
    static constexpr int kIdleMs        = 60000;
    static constexpr int kCountdownSecs = 60;
    // Above this the car is moving — never shut down, even with the engine
    // apparently off (a dead CAN adapter mid-session reads as "stopped").
    static constexpr int kMaxSpeedKmh   = 5;
    // After `systemctl poweroff` is accepted, systemd stops this service
    // within seconds. Still running after this long means the shutdown
    // stalled or was cancelled — give the dash back rather than sit on the
    // "shutting down" overlay forever.
    static constexpr int kPoweroffWatchdogMs = 60000;

    PowerModel(CanDataModel *canModel, bool mockMode, QObject *parent = nullptr);

    bool shutdownPending() const { return m_countdownTimer.isActive(); }
    int  secondsRemaining() const { return m_secondsRemaining; }
    bool shuttingDown() const { return m_shuttingDown; }
    bool armed() const { return m_armed; }

    // The object whose input counts as interaction — the QQuickWindow in
    // production. Watched only while armed. Not the whole application: that
    // would put every timer and paint event through the filter.
    void setInputSource(QObject *source);

    // Test seam: shorter idle wait / countdown. tickMs is one countdown step.
    void setTimings(int idleMs, int countdownSecs, int tickMs);

    // Something that must not be interrupted by a poweroff (an update install).
    void setBlocked(bool blocked);

    // RaceBox GPS fix. A GPS speed only counts as standstill evidence with a
    // fix: a dropout reports 0 km/h.
    void setGpsFix(bool hasFix);

    // The driver is still here — hide the countdown and restart the idle wait.
    Q_INVOKABLE void cancelShutdown();

signals:
    void shutdownPendingChanged();
    void secondsRemainingChanged();
    void shuttingDownChanged();
    // Emitted synchronously right before poweroff — last chance to flush.
    void aboutToPowerOff();
    // The poweroff after aboutToPowerOff() failed, or was a mock no-op.
    void powerOffAborted();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateArmed();
    void updateMotion();
    bool conditionsHold() const;
    void arm();
    void disarm();
    void startCountdown();
    void stopCountdown();
    void onCountdownTick();
    void performShutdown();
    void abortShutdown(const QString &reason, bool flushed);
    void setSecondsRemaining(int secs);
    void setShuttingDown(bool shuttingDown);

    CanDataModel *m_canModel = nullptr;
    QObject *m_inputSource = nullptr;
    bool m_mockMode = false;
    bool m_blocked = false;
    bool m_armed = false;
    bool m_shuttingDown = false;
    bool m_seenMoving = false; // latched; see the class comment
    bool m_gpsFix = false;
    int  m_secondsRemaining = 0;
    int  m_countdownSecs = kCountdownSecs;
    QTimer m_idleTimer;      // single-shot, restarted on every interaction
    QTimer m_countdownTimer; // one tick per second; running == countdown showing
    QTimer m_poweroffWatchdog; // single-shot, started once poweroff is accepted
};
