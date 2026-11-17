#include "src/device/powermodel.h"
#include "src/can/candatamodel.h"
#include "src/core/logging.h"
#include "src/core/procrunner.h"

#include <QEvent>
#include <unistd.h>

namespace {
// Long enough for the "shutting down" frame to reach the screen before the
// synchronous session save blocks the event loop.
constexpr int kPaintDelayMs = 100;
constexpr int kPoweroffTimeoutMs = 10000;
}

PowerModel::PowerModel(CanDataModel *canModel, bool mockMode, QObject *parent)
    : QObject(parent)
    , m_canModel(canModel)
    , m_mockMode(mockMode)
{
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(kIdleMs);
    connect(&m_idleTimer, &QTimer::timeout, this, &PowerModel::startCountdown);

    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, &PowerModel::onCountdownTick);

    connect(m_canModel, &CanDataModel::engineRunningChanged, this, &PowerModel::updateArmed);
    connect(m_canModel, &CanDataModel::speedChanged, this, &PowerModel::updateArmed);
    updateArmed();
}

void PowerModel::setInputSource(QObject *source)
{
    if (source == m_inputSource)
        return;
    if (m_armed && m_inputSource)
        m_inputSource->removeEventFilter(this);
    m_inputSource = source;
    if (m_armed && m_inputSource)
        m_inputSource->installEventFilter(this);
}

void PowerModel::setTimings(int idleMs, int countdownSecs, int tickMs)
{
    m_idleTimer.setInterval(idleMs);
    m_countdownSecs = countdownSecs;
    m_countdownTimer.setInterval(tickMs);
    if (m_armed && !shutdownPending())
        m_idleTimer.start();
}

void PowerModel::setBlocked(bool blocked)
{
    if (blocked == m_blocked)
        return;
    m_blocked = blocked;
    updateArmed();
}

bool PowerModel::conditionsHold() const
{
    return !m_blocked
        && !m_canModel->engineRunning()
        && m_canModel->speed() <= kMaxSpeedKmh;
}

void PowerModel::updateArmed()
{
    // While shutting down, the conditions are re-checked once more right
    // before poweroff (performShutdown()) rather than here.
    if (m_shuttingDown)
        return;
    const bool shouldArm = conditionsHold();
    if (shouldArm == m_armed)
        return;
    if (shouldArm)
        arm();
    else
        disarm();
}

void PowerModel::arm()
{
    m_armed = true;
    if (m_inputSource)
        m_inputSource->installEventFilter(this);
    m_idleTimer.start();
    qCInfo(lcApp) << "[Power] engine off — idle shutdown armed";
}

void PowerModel::disarm()
{
    m_armed = false;
    if (m_inputSource)
        m_inputSource->removeEventFilter(this);
    m_idleTimer.stop();
    stopCountdown();
    qCInfo(lcApp) << "[Power] idle shutdown disarmed";
}

bool PowerModel::eventFilter(QObject *watched, QEvent *event)
{
    // Only ever restarts the idle wait. Once the countdown shows, cancelling
    // belongs to the overlay: cancelling here would hide it before the window
    // delivers this same press, and the tap would land on the button beneath.
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::KeyPress:
        if (!shutdownPending() && !m_shuttingDown)
            m_idleTimer.start();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event); // observe only, never consume
}

void PowerModel::cancelShutdown()
{
    if (!m_armed || m_shuttingDown)
        return;
    if (shutdownPending()) {
        qCInfo(lcApp) << "[Power] idle shutdown cancelled by user";
        stopCountdown();
    }
    m_idleTimer.start();
}

void PowerModel::startCountdown()
{
    qCInfo(lcApp) << "[Power] no interaction with the engine off — shutting down in"
                  << m_countdownSecs << "s unless touched";
    setSecondsRemaining(m_countdownSecs);
    m_countdownTimer.start();
    emit shutdownPendingChanged();
}

void PowerModel::stopCountdown()
{
    if (!m_countdownTimer.isActive())
        return;
    m_countdownTimer.stop();
    emit shutdownPendingChanged();
}

void PowerModel::onCountdownTick()
{
    setSecondsRemaining(m_secondsRemaining - 1);
    if (m_secondsRemaining <= 0)
        performShutdown();
}

void PowerModel::performShutdown()
{
    m_idleTimer.stop();
    setShuttingDown(true);
    stopCountdown();

    QTimer::singleShot(kPaintDelayMs, this, [this]() {
        // Last look before committing: the engine may have been restarted, or
        // the car moved, in the moment since the countdown ran out.
        if (!conditionsHold()) {
            abortShutdown(QStringLiteral("engine started or car moving"), false);
            return;
        }

        qCInfo(lcApp) << "[Power] idle shutdown — saving and powering off";
        emit aboutToPowerOff();
        ::sync(); // whatever aboutToPowerOff wrote is on the card before poweroff starts

        if (m_mockMode) {
            qCInfo(lcApp) << "[mock] poweroff requested — no-op";
            QTimer::singleShot(2000, this, [this]() { abortShutdown(QStringLiteral("mock mode"), true); });
            return;
        }

        // No sudo: install.sh's polkit rule lets the dashboard user power off.
        Proc::run(this, QStringLiteral("systemctl"), {QStringLiteral("poweroff")}, kPoweroffTimeoutMs,
                  [this](bool ok, int, const QString &out) {
                      if (!ok)
                          abortShutdown(Proc::lastNonEmptyLine(out), true);
                      // else: the system is going down
                  });
    });
}

void PowerModel::abortShutdown(const QString &reason, bool flushed)
{
    // Back to watching for idle, so a refused poweroff retries after the next
    // full idle period instead of leaving the dash stuck on "shutting down".
    if (!m_mockMode)
        qCWarning(lcApp).noquote() << "[Power] poweroff aborted:" << reason;
    else
        qCInfo(lcApp).noquote() << "[Power] poweroff aborted:" << reason;
    setShuttingDown(false);
    disarm();
    if (flushed)
        emit powerOffAborted(); // let what aboutToPowerOff shut down come back up
    updateArmed();
}

void PowerModel::setSecondsRemaining(int secs)
{
    if (secs == m_secondsRemaining)
        return;
    m_secondsRemaining = secs;
    emit secondsRemainingChanged();
}

void PowerModel::setShuttingDown(bool shuttingDown)
{
    if (shuttingDown == m_shuttingDown)
        return;
    m_shuttingDown = shuttingDown;
    emit shuttingDownChanged();
}
