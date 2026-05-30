#pragma once

#include <QtEndian>
#include <QtGlobal>

// Encode/decode pairs for each PT-CAN frame. Both MockCanProvider and
// CanDataModel use these so a single edit covers both sides of the wire.
namespace CanScaling {

constexpr quint32 kFrameRpm  = 0x316;
constexpr quint32 kFrameTemp = 0x329;
// EGS (ZF automatic) gear message — byte 0 = gear (0=N, 1-6=gear, 7=R)
// Manual gearbox has no gear sensor; mock uses this frame for simulation only
constexpr quint32 kFrameGear = 0x43F;

// Byte offsets within each frame — single source of truth for encoder and decoder
constexpr int kOffsetRpm     = 1; // 0x316 bytes 1-2, BE u16
constexpr int kOffsetCoolant = 1; // 0x329 byte 1
constexpr int kOffsetOil     = 2; // 0x329 byte 2
constexpr int kOffsetSpeed   = 4; // 0x329 bytes 4-5, BE u16

inline quint16 encodeRpm(int rpm)       { return static_cast<quint16>(rpm  * 6.42); }
inline int     decodeRpm(quint16 raw)   { return static_cast<int>(raw / 6.42); }

// 0x329 byte formula: 0.75 * byte - 48.373 °C
inline quint8  encodeTemp(double c)     { return static_cast<quint8>((c + 48.373) / 0.75); }
inline double  decodeTemp(quint8 b)     { return 0.75 * b - 48.373; }

// 0x329 speed: wire unit is km/h, no scaling needed
inline quint16 encodeSpeed(int kmh)     { return static_cast<quint16>(kmh); }
inline int     decodeSpeed(quint16 raw) { return static_cast<int>(raw); }

inline quint8 encodeGear(int gear)  { return static_cast<quint8>(gear < 0 ? 7 : gear); }
inline int    decodeGear(quint8 b)  { return b == 7 ? -1 : static_cast<int>(b); }

} // namespace CanScaling
