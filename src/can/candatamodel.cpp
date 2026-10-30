#include "src/can/candatamodel.h"
#include "src/core/canscaling.h"
#include "src/core/logging.h"
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
            m_rpmClock.start();
            // At 50 Hz frame rate, a >2000 RPM single-frame drop is a CAN transient,
            // not a real deceleration event — reject it. Only one frame in a
            // row, though: a second consecutive low reading is real, and must
            // not be rejected forever behind a bogus high value.
            if (m_rpm > 600 && rpm < m_rpm - 2000 && !m_rpmDropRejected) {
                m_rpmDropRejected = true;
                break;
            }
            m_rpmDropRejected = false;
            if (rpm == m_rpm)
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
            // Init/PVS error reads as a closed pedal, never the pre-fault value.
            updateInput(m_throttle, CanScaling::decodeThrottle(raw),
                        CanScaling::throttleRawValid(raw), kDirtyThrottle);
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
        if (p.size() >= CanScaling::kOffsetDme4Status + 1) {
            const bool low = static_cast<quint8>(p[CanScaling::kOffsetDme4Status]) & CanScaling::kOilPressureLowBit;
            updateInput(m_oilPressureLow, low, true, 0);
        } else if (!m_dme4ShortWarned) {
            // Oil temp still decodes, but the oil pressure warning can't work
            // without byte 7 — make that visible in the device log.
            m_dme4ShortWarned = true;
            qCWarning(lcCan) << "0x545 frame has" << p.size()
                             << "bytes — oil pressure warning unavailable";
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
            updateAbsFromWheels();
        }
        break;

    case CanScaling::kFrameSteering:
        if (p.size() >= CanScaling::kOffsetSteering + 2) {
            updateInput(m_steering, CanScaling::decodeSteering(p.constData() + CanScaling::kOffsetSteering),
                        true, kDirtySteering);
        }
        break;

    case CanScaling::kFrameAsc1:
        if (p.size() >= CanScaling::kOffsetAsc1Status + 1) {
            const auto status = static_cast<quint8>(p[CanScaling::kOffsetAsc1Status]);
            updateInput(m_brake, bool(status & CanScaling::kBrakeBit), true, kDirtyBrake);
            updateInput(m_absLampRaw, bool(status & CanScaling::kAbsLedBit), true, 0);
            logAsc1StatusBits(status);
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

template <typename T>
void CanDataModel::updateInput(Input<T> &in, T value, bool valid, quint16 dirtyBit)
{
    in.clock.start();
    if (!valid)
        value = T{};
    if (valid != in.valid || value != in.value) {
        in.valid = valid;
        in.value = value;
        m_dirty |= dirtyBit;
    }
}

template <typename T>
void CanDataModel::expireInput(Input<T> &in, quint16 dirtyBit)
{
    // A frame that stopped arriving drops its input back to the neutral
    // default and flags it invalid, instead of holding the last reading.
    if (in.valid && !fresh(in.clock, kInputStaleMs)) {
        in.valid = false;
        in.value = T{};
        m_dirty |= dirtyBit;
    }
}

void CanDataModel::expireStaleInputs()
{
    expireInput(m_throttle, kDirtyThrottle);
    expireInput(m_steering, kDirtySteering);
    expireInput(m_brake, kDirtyBrake);
    expireInput(m_oilPressureLow, 0);
    expireInput(m_absLampRaw, 0);
}

bool CanDataModel::engineRunning() const
{
    // A stale rpm (0x316 gone quiet) can't vouch for a running engine.
    return fresh(m_rpmClock, kInputStaleMs) && m_rpm > kOilPressureMinRpm;
}

bool CanDataModel::Debounced::update(bool raw, qint64 riseMs, qint64 fallMs)
{
    if (raw == state) {
        pending.invalidate();
        return false;
    }
    if (!pending.isValid())
        pending.start();
    if (pending.elapsed() < (raw ? riseMs : fallMs))
        return false;
    state = raw;
    pending.invalidate();
    return true;
}

void CanDataModel::updateOilPressureWarning()
{
    const bool low = m_oilPressureLow.value && engineRunning();
    if (m_oilPressureWarning.update(low, kOilPressureDebounceMs, kOilPressureDebounceMs))
        m_dirty |= kDirtyOilPressure;
}

bool CanDataModel::lagging(double kmh, double ref)
{
    return (ref - kmh) / ref >= kAbsSlipRatio;
}

bool CanDataModel::wheelsSlipping() const
{
    // The fastest wheel is the best road-speed reference under braking (same
    // reasoning as vehicleSpeedFromWheels()); a wheel well below it is one the
    // ABS is fighting to keep from locking.
    if (!m_brake.live())
        return false;
    double ref = 0.0;
    for (int i = 0; i < 4; ++i)
        if (!m_wheelImplausible[i])
            ref = std::max(ref, m_wheels[i]);
    if (ref < kAbsMinSpeedKmh)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!m_wheelImplausible[i] && lagging(m_wheels[i], ref))
            return true;
    return false;
}

void CanDataModel::updateImplausibleWheels()
{
    // A wheel only legitimately lags under braking. One that lags while the
    // car rolls unbraked is a dead or failing sensor / tone ring — exclude it
    // from the slip check, or every brake press would show as ABS regulating.
    // Only re-judged while rolling unbraked, so the mask holds through a brake
    // zone; above kAbsMinSpeedKmh cornering can't make a wheel lag.
    const double fastest = fastestWheel();
    if (m_brake.live() || fastest < kAbsMinSpeedKmh)
        return;
    for (int i = 0; i < 4; ++i)
        m_wheelImplausible[i] = lagging(m_wheels[i], fastest);
}

void CanDataModel::updateAbsFromWheels()
{
    updateImplausibleWheels();
    if (fastestWheel() >= kAbsFaultMinSpeedKmh)
        m_hasRolled = true;
    updateAbsActive();
}

void CanDataModel::updateAbsActive()
{
    // Called per 0x1F0 frame. Needs kAbsSlipFrames in a row to light, so one
    // noisy frame can't; once lit, any slipping frame extends the hold.
    if (!wheelsSlipping()) {
        m_absSlipFrames = 0;
        return;
    }
    // Slip frames either side of a 0x1F0 gap aren't consecutive.
    if (!fresh(m_absLastSlip, kAbsSlipGapMs))
        m_absSlipFrames = 0;
    m_absLastSlip.start();
    m_absSlipFrames = std::min(m_absSlipFrames + 1, kAbsSlipFrames);
    if (m_absSlipFrames == kAbsSlipFrames && !m_absActive) {
        m_absActive = true;
        m_dirty |= kDirtyAbsActive;
    }
}

void CanDataModel::expireAbsActive()
{
    // Slip is only evaluated on 0x1F0, so this also covers the wheel speeds or
    // the brake switch going quiet — no fresh slip frame, the hold runs out.
    if (m_absActive && !fresh(m_absLastSlip, kAbsHoldMs)) {
        m_absActive = false;
        m_dirty |= kDirtyAbsActive;
    }
}

void CanDataModel::logAsc1StatusBits(quint8 byte0)
{
    // LV_BAS / LV_EBV / LV_ABS_LED are undescribed in the ms4x ASC1 layout.
    // Log each change so a hard stop on track shows in the Device Log whether
    // any of them actually tracks ABS regulation.
    // TODO: remove (with kBasBit/kEbvBit) once the bits are verified on the car.
    if (m_asc1StatusLogged > kAsc1StatusLogLimit)
        return;
    const int bits = byte0 & (CanScaling::kBasBit | CanScaling::kEbvBit | CanScaling::kAbsLedBit);
    if (bits == m_asc1StatusBits)
        return;
    m_asc1StatusBits = bits;
    if (++m_asc1StatusLogged > kAsc1StatusLogLimit) {
        qCInfo(lcCan) << "ASC1 status: further changes not logged";
        return;
    }
    qCInfo(lcCan).nospace() << "ASC1 status: BAS=" << bool(bits & CanScaling::kBasBit)
                            << " EBV=" << bool(bits & CanScaling::kEbvBit)
                            << " ABS_LED=" << bool(bits & CanScaling::kAbsLedBit);
}

double CanDataModel::fastestWheel() const
{
    return *std::max_element(m_wheels.begin(), m_wheels.end());
}

int CanDataModel::vehicleSpeedFromWheels() const
{
    // RWD: the undriven fronts don't overread under wheelspin. Under braking a
    // locking wheel underreads instead, so take the fastest of all four — the
    // one closest to true road speed.
    const double kmh = m_brake.live()
        ? fastestWheel()
        : (m_wheels[FL] + m_wheels[FR]) / 2.0;
    // The MK60 reports a small non-zero offset at standstill; don't let it
    // round up to a phantom 1 km/h.
    if (kmh < kWheelSpeedDeadbandKmh)
        return 0;
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
    updateOilPressureWarning();
    expireAbsActive();
    // Rise debounce rides out the key-on bulb check; clears immediately.
    if (m_absFault.update(m_absLampRaw.value && m_hasRolled, kAbsFaultDebounceMs, 0))
        m_dirty |= kDirtyAbsFault;

    if (!m_dirty)
        return;
    const quint16 dirty = m_dirty;
    m_dirty = 0;

    if (dirty & kDirtyCoolant) emit coolantTempChanged();
    if (dirty & kDirtyOilTemp) emit oilTempChanged();
    if (dirty & kDirtyOilSeen) emit oilTempSeenChanged();
    if (dirty & kDirtyOilPressure) emit oilPressureWarningChanged();
    if (dirty & kDirtyAbsActive)   emit absActiveChanged();
    if (dirty & kDirtyAbsFault)    emit absFaultChanged();
    if (dirty & kDirtySpeed)   emit speedChanged();
    if (dirty & kDirtyGear)    emit gearChanged();
    if (dirty & kDirtySpeedSource) emit speedFromWheelsChanged();
    if (dirty & kDirtyWheelSpeeds) emit wheelSpeedsChanged();
    if (dirty & kDirtySteering)    emit steeringAngleChanged();
    if (dirty & kDirtyBrake)       emit brakePressedChanged();
    if (dirty & kDirtyThrottle)    emit throttleChanged();
}
