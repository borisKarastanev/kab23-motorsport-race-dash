#pragma once

#include <QtEndian>
#include <QtGlobal>
#include <cmath>

// Encode/decode pairs for each PT-CAN frame. Both MockCanProvider and
// CanDataModel use these so a single edit covers both sides of the wire.
namespace CanScaling {

constexpr quint32 kFrameRpm  = 0x316;
constexpr quint32 kFrameTemp = 0x329;
// DME4 status frame — byte 4 = oil temperature (TOIL_CAN)
constexpr quint32 kFrameDme4 = 0x545;

// Byte offsets within each frame — single source of truth for encoder and decoder
constexpr int kOffsetRpm     = 2; // 0x316 bytes 2-3, LE u16 (byte 2 = LSB, byte 3 = MSB)
constexpr int kOffsetCoolant = 1; // 0x329 byte 1
constexpr int kOffsetOilTemp = 4; // 0x545 byte 4

// 0x316 RPM: DME spec N_ENG = raw * 0.15625  →  kRawPerRpm = 6.4 (exact)
// Empirical candump read ~6.42; the <0.3% difference is below display resolution.
constexpr double kRawPerRpm = 6.4;
inline void encodeRpm(int rpm, char *dst) { qToLittleEndian<quint16>(static_cast<quint16>(rpm * kRawPerRpm), dst); }
inline int  decodeRpm(const char *src)    { return static_cast<int>(qFromLittleEndian<quint16>(src) / kRawPerRpm); }

// 0x329 coolant: 0.75 * byte - 48.373 °C  (MS43 DME sensor calibration)
constexpr double kCoolantScale  = 0.75;
constexpr double kCoolantOffset = 48.373;
inline quint8  encodeCoolant(double c)     { return static_cast<quint8>((c + kCoolantOffset) / kCoolantScale); }
inline double  decodeCoolant(quint8 b)     { return kCoolantScale * b - kCoolantOffset; }

// 0x329 byte 5 accelerator pedal (TPS_CAN, % of PVS_MAX). Spec scale is
// raw * 0.390625 with 0x01 = 0 % and 0xFE = 99.2 %; normalised here so the
// usable range 0x01..0xFE maps to a full 0..100 %. 0x00 = init, 0xFF = error.
constexpr int    kOffsetThrottle   = 5;
constexpr quint8 kThrottleRawMin   = 0x01;
constexpr quint8 kThrottleRawMax   = 0xFE;
constexpr quint8 kThrottleRawInit  = 0x00;
constexpr quint8 kThrottleRawError = 0xFF;
inline bool   throttleRawValid(quint8 b) { return b != kThrottleRawInit && b != kThrottleRawError; }
inline double decodeThrottle(quint8 b)
{
    const double pct = (static_cast<double>(b) - kThrottleRawMin) * 100.0 / (kThrottleRawMax - kThrottleRawMin);
    return qBound(0.0, pct, 100.0);
}
inline quint8 encodeThrottle(double pct)
{
    const int raw = kThrottleRawMin + qRound(qBound(0.0, pct, 100.0) * (kThrottleRawMax - kThrottleRawMin) / 100.0);
    return static_cast<quint8>(raw);
}

// 0x545 oil temp: byte - 48 °C  (TOIL_CAN, DME4 spec — different formula from coolant)
constexpr double kOilTempOffset = 48.0;
inline quint8  encodeOilTemp(double c)     { return static_cast<quint8>(c + kOilTempOffset); }
inline double  decodeOilTemp(quint8 b)     { return static_cast<double>(b) - kOilTempOffset; }

// ── MK60 ABS (retrofit) ─────────────────────────────────────────────────────
// Layouts below are the assumed E46 DSC encoding — verify against candump.

// 0x1F0 wheel speeds: four LE u16 at bytes 0/2/4/6 (FL, FR, RL, RR). Low 12 bits
// are the speed; the upper nibble carries counter/flag bits and is masked off.
constexpr quint32 kFrameWheelSpeed   = 0x1F0;
constexpr int     kOffsetWheelFL     = 0;
constexpr int     kOffsetWheelFR     = 2;
constexpr int     kOffsetWheelRL     = 4;
constexpr int     kOffsetWheelRR     = 6;
constexpr quint16 kWheelSpeedMask    = 0x0FFF;
constexpr double  kKmhPerWheelLsb    = 0.0625;
inline void   encodeWheelSpeed(double kmh, char *dst)
{
    // Clamp, don't mask: masking wraps anything above ~256 km/h to a tiny value.
    const int raw = qBound(0, qRound(kmh / kKmhPerWheelLsb), int(kWheelSpeedMask));
    qToLittleEndian<quint16>(static_cast<quint16>(raw), dst);
}
inline double decodeWheelSpeed(const char *src)
{
    return (qFromLittleEndian<quint16>(src) & kWheelSpeedMask) * kKmhPerWheelLsb;
}

// 0x1F5 steering angle (LWS): bytes 0-1 LE u16, bits 0-14 magnitude, bit 15 = sign
// (set = negative). Only meaningful once the LWS has been calibrated.
constexpr quint32 kFrameSteering      = 0x1F5;
constexpr int     kOffsetSteering     = 0;
constexpr quint16 kSteeringSignBit    = 0x8000;
constexpr double  kDegPerSteeringLsb  = 0.045;
inline void   encodeSteering(double deg, char *dst)
{
    auto raw = static_cast<quint16>(qRound(std::abs(deg) / kDegPerSteeringLsb)) & ~kSteeringSignBit;
    if (deg < 0)
        raw |= kSteeringSignBit;
    qToLittleEndian<quint16>(static_cast<quint16>(raw), dst);
}
inline double decodeSteering(const char *src)
{
    const quint16 raw = qFromLittleEndian<quint16>(src);
    const double mag  = (raw & ~kSteeringSignBit) * kDegPerSteeringLsb;
    return (raw & kSteeringSignBit) ? -mag : mag;
}

// 0x153 ASC1: byte 0 bit 4 = brake light switch
constexpr quint32 kFrameAsc1   = 0x153;
constexpr int     kOffsetBrake = 0;
constexpr quint8  kBrakeBit    = 0x10;

} // namespace CanScaling
