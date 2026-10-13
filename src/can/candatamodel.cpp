#include "src/can/candatamodel.h"
#include "src/core/canscaling.h"
#include <algorithm>
#include <cmath>

CanDataModel::CanDataModel(QObject *parent)
    : QObject(parent)
{
    m_notifyTimer.setInterval(100); // 10 Hz — decouples CAN bus rate from QML update rate
    connect(&m_notifyTimer, &QTimer::timeout, this, &CanDataModel::emitNotifications);
    m_notifyTimer.start();
}

void CanDataModel::onFrame(const QCanBusFrame &frame)
{
    const QByteArray &p = frame.payload();

    switch (frame.frameId()) {

    case CanScaling::kFrameRpm:
        if (p.size() >= CanScaling::kOffsetRpm + 2) {
            int rpm = CanScaling::decodeRpm(p.constData() + CanScaling::kOffsetRpm);
            rpm = qMin(rpm, 7000);
            // At 50 Hz frame rate, a >2000 RPM single-frame drop is a CAN transient,
            // not a real deceleration event — reject it.
            if ((m_rpm > 600 && rpm < m_rpm - 2000) || rpm == m_rpm)
                break;
            m_rpm = rpm;
            emit rpmChanged(); // bypass the 10 Hz timer — RPM needs immediate QML updates
        }
        break;

    case CanScaling::kFrameTemp:
        if (p.size() >= CanScaling::kOffsetCoolant + 1) {
            m_coolantTemp = CanScaling::decodeCoolant(static_cast<quint8>(p[CanScaling::kOffsetCoolant]));
            m_dirty |= kDirtyCoolant;
        }
        if (p.size() >= CanScaling::kOffsetThrottle + 1) {
            const auto raw = static_cast<quint8>(p[CanScaling::kOffsetThrottle]);
            const bool valid = CanScaling::throttleRawValid(raw);
            // Init/PVS error reads as a closed pedal, never the pre-fault value.
            const double pct = valid ? CanScaling::decodeThrottle(raw) : 0.0;
            m_throttleClock.start();
            if (valid != m_throttleValid || pct != m_throttle) {
                m_throttleValid = valid;
                m_throttle = pct;
                m_dirty |= kDirtyThrottle;
            }
        }
        break;

    case CanScaling::kFrameDme4:
        if (p.size() >= CanScaling::kOffsetOilTemp + 1) {
            m_oilTemp = CanScaling::decodeOilTemp(static_cast<quint8>(p[CanScaling::kOffsetOilTemp]));
            m_dirty |= kDirtyOilTemp;
            if (!m_oilTempSeen) {
                m_oilTempSeen = true;
                m_dirty |= kDirtyOilSeen;
            }
        }
        break;

    case CanScaling::kFrameWheelSpeed:
        if (p.size() >= CanScaling::kOffsetWheelRR + 2) {
            std::array<double, 4> wheels;
            for (int i = 0; i < 4; ++i)  // FL, FR, RL, RR — two bytes each
                wheels[i] = CanScaling::decodeWheelSpeed(p.constData() + CanScaling::kOffsetWheelFL + 2 * i);
            if (wheels != m_wheels) {
                m_wheels = wheels;
                m_dirty |= kDirtyWheelSpeeds;
            }
            m_wheelSpeedClock.start();

            if (!m_speedFromWheels) {
                m_speedFromWheels = true;
                m_dirty |= kDirtySpeedSource;
            }
            applySpeed(vehicleSpeedFromWheels());
        }
        break;

    case CanScaling::kFrameSteering:
        if (p.size() >= CanScaling::kOffsetSteering + 2) {
            const double angle = CanScaling::decodeSteering(p.constData() + CanScaling::kOffsetSteering);
            m_steeringClock.start();
            if (angle != m_steeringAngle || !m_steeringValid) {
                m_steeringAngle = angle;
                m_steeringValid = true;
                m_dirty |= kDirtySteering;
            }
        }
        break;

    case CanScaling::kFrameAsc1:
        if (p.size() >= CanScaling::kOffsetBrake + 1) {
            const bool pressed = static_cast<quint8>(p[CanScaling::kOffsetBrake]) & CanScaling::kBrakeBit;
            m_brakeClock.start();
            if (pressed != m_brakePressed || !m_brakeValid) {
                m_brakePressed = pressed;
                m_brakeValid = true;
                m_dirty |= kDirtyBrake;
            }
        }
        break;

    default:
        break;
    }
}

bool CanDataModel::fresh(const QElapsedTimer &clock, qint64 staleMs)
{
    return clock.isValid() && clock.elapsed() < staleMs;
}

bool CanDataModel::wheelSpeedFresh() const
{
    return fresh(m_wheelSpeedClock, kWheelSpeedStaleMs);
}

void CanDataModel::expireStaleInputs()
{
    // A frame that stopped arriving drops its input back to the neutral
    // default and flags it invalid, instead of holding the last reading.
    if (m_throttleValid && !fresh(m_throttleClock, kInputStaleMs)) {
        m_throttleValid = false;
        m_throttle = 0.0;
        m_dirty |= kDirtyThrottle;
    }
    if (m_steeringValid && !fresh(m_steeringClock, kInputStaleMs)) {
        m_steeringValid = false;
        m_steeringAngle = 0.0;
        m_dirty |= kDirtySteering;
    }
    // Also stops a brake bit latched on by a dead ASC1 from pinning the speed
    // to the fastest wheel.
    if (m_brakeValid && !fresh(m_brakeClock, kInputStaleMs)) {
        m_brakeValid = false;
        m_brakePressed = false;
        m_dirty |= kDirtyBrake;
    }
}

int CanDataModel::vehicleSpeedFromWheels() const
{
    // RWD: the undriven fronts don't overread under wheelspin. Under braking a
    // locking wheel underreads instead, so take the fastest of all four — the
    // one closest to true road speed.
    const double kmh = m_brakePressed
        ? *std::max_element(m_wheels.begin(), m_wheels.end())
        : (m_wheels[FL] + m_wheels[FR]) / 2.0;
    return static_cast<int>(std::lround(kmh));
}

void CanDataModel::applySpeed(int kmh)
{
    if (kmh == m_speed)
        return;
    m_speed = kmh;
    m_dirty |= kDirtySpeed;
}

void CanDataModel::updateSpeedSource()
{
    // Wheel speeds went quiet (ABS unplugged, frame filtered) — hand the gauge
    // back to the GPS so it doesn't hold the last wheel reading forever. The
    // single place GPS speed reaches the gauge, so speed and speedFromWheels
    // never disagree.
    if (m_speedFromWheels && !wheelSpeedFresh()) {
        m_speedFromWheels = false;
        m_dirty |= kDirtySpeedSource;
    }
    if (!m_speedFromWheels)
        applySpeed(m_gpsSpeed);
}

void CanDataModel::setSpeed(int kmh)
{
    // RaceBox GPS speed — always remembered, but only displayed when the MK60
    // wheel speeds aren't arriving.
    m_gpsSpeed = kmh;
    updateSpeedSource();
}

void CanDataModel::emitNotifications()
{
    updateSpeedSource();
    expireStaleInputs();

    if (!m_dirty)
        return;
    const quint16 dirty = m_dirty;
    m_dirty = 0;

    if (dirty & kDirtyCoolant) emit coolantTempChanged();
    if (dirty & kDirtyOilTemp) emit oilTempChanged();
    if (dirty & kDirtyOilSeen) emit oilTempSeenChanged();
    if (dirty & kDirtySpeed)   emit speedChanged();
    if (dirty & kDirtyGear)    emit gearChanged();
    if (dirty & kDirtySpeedSource) emit speedFromWheelsChanged();
    if (dirty & kDirtyWheelSpeeds) emit wheelSpeedsChanged();
    if (dirty & kDirtySteering)    emit steeringAngleChanged();
    if (dirty & kDirtyBrake)       emit brakePressedChanged();
    if (dirty & kDirtyThrottle)    emit throttleChanged();
}
