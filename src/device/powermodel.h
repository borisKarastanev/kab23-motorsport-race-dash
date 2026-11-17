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
// nothing has blocked it (an update install in progress). While disarmed
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
    int  m_secondsRemaining = 0;
    int  m_countdownSecs = kCountdownSecs;
    QTimer m_idleTimer;      // single-shot, restarted on every interaction
    QTimer m_countdownTimer; // one tick per second; running == countdown showing
};
