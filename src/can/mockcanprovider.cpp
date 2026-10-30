#include "src/can/mockcanprovider.h"
#include "src/can/canframes.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr int SHIFT_RPM   = 6800;
constexpr int IDLE_RPM    =  650;
constexpr int CYCLE_TICKS = 1150; // 23.0 s at 50 Hz
constexpr int STEER_TICKS =  400; //  8.0 s at 50 Hz
constexpr double KMH_PER_RPM = 0.03; // rough single-gear correlation: 6800 rpm ≈ 204 km/h
constexpr double ABS_MIN_KMH = 80.0; // simulated ABS stops only above this speed
constexpr int    ABS_PULSE_TICKS = 5; // 100 ms modulation windows at 50 Hz

} // namespace

static constexpr quint64 COOLANT_RAMP_TICKS = 3000; // 60 s at 50 Hz
static constexpr quint64 OIL_RAMP_TICKS     = 4500; // 90 s at 50 Hz

MockCanProvider::MockCanProvider(QObject *parent)
    : ICanProvider(parent)
    , m_oilPressureLow(qEnvironmentVariableIsSet("MOCK_OIL_PRESSURE_LOW"))
    , m_absFault(qEnvironmentVariableIsSet("MOCK_ABS_FAULT"))
{
    m_timer.setInterval(20); // 50 Hz
    connect(&m_timer, &QTimer::timeout, this, &MockCanProvider::onTick);
}

void MockCanProvider::start() { m_tick = 0; m_prevRpm = 0; m_timer.start(); }
void MockCanProvider::stop()  { m_timer.stop(); }

void MockCanProvider::onTick()
{
    ++m_tick;

    // RPM: sine wave between idle and redline over the cycle period
    const double phase = (static_cast<double>(m_tick % CYCLE_TICKS) / CYCLE_TICKS) * 2.0 * M_PI;
    const int rpm = IDLE_RPM + static_cast<int>((SHIFT_RPM - IDLE_RPM) * 0.5 * (1.0 - std::cos(phase)));

    const double coolantProgress = std::min(static_cast<double>(m_tick) / COOLANT_RAMP_TICKS, 1.0);
    const double oilProgress     = std::min(static_cast<double>(m_tick) / OIL_RAMP_TICKS,     1.0);

    emit frameReady(CanFrames::rpm(rpm));
    // Throttle: on while revs climb (first half of the cycle), off while they fall.
    const double throttle = std::max(0.0, 100.0 * std::sin(phase));
    emit frameReady(CanFrames::dme2(20.0 + coolantProgress * 70.0, throttle));
    emit frameReady(CanFrames::dme4(20.0 + oilProgress * 90.0, m_oilPressureLow));

    // Wheel speeds track RPM; rears read ~2% high to mimic driven-wheel slip.
    // Hard braking at speed simulates ABS: FL dips 30% on alternate 100 ms
    // windows, like a wheel the MK60 is releasing and reapplying.
    const double kmh = rpm * KMH_PER_RPM;
    const bool braking = rpm < m_prevRpm; // braking while revs fall
    // Brake switch first, as it would be on the bus ahead of the slip it causes.
    emit frameReady(CanFrames::asc1((braking ? CanScaling::kBrakeBit : 0)
                                    | (m_absFault ? CanScaling::kAbsLedBit : 0)));
    const bool absDip = braking && kmh > ABS_MIN_KMH && (m_tick / ABS_PULSE_TICKS) % 2 == 0;
    emit frameReady(CanFrames::wheelSpeeds(absDip ? kmh * 0.7 : kmh, kmh, kmh * 1.02, kmh * 1.02));

    const double steerPhase = (static_cast<double>(m_tick % STEER_TICKS) / STEER_TICKS) * 2.0 * M_PI;
    emit frameReady(CanFrames::steering(90.0 * std::sin(steerPhase)));

    m_prevRpm = rpm;
}
