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
// DME4 status frame — byte 4 = oil temperature (TOIL_CAN)
constexpr quint32 kFrameDme4 = 0x545;

// Byte offsets within each frame — single source of truth for encoder and decoder
constexpr int kOffsetRpm     = 1; // 0x316 bytes 1-2, BE u16
constexpr int kOffsetCoolant = 1; // 0x329 byte 1
constexpr int kOffsetSpeed   = 4; // 0x329 bytes 4-5, BE u16
constexpr int kOffsetOilTemp = 4; // 0x545 byte 4

// 0x316 RPM: raw = rpm * 6.42
constexpr double kRpmScale = 6.42;
inline quint16 encodeRpm(int rpm)       { return static_cast<quint16>(rpm  * kRpmScale); }
inline int     decodeRpm(quint16 raw)   { return static_cast<int>(raw / kRpmScale); }

// 0x329 coolant: 0.75 * byte - 48.373 °C  (MS43 DME sensor calibration)
constexpr double kCoolantScale  = 0.75;
constexpr double kCoolantOffset = 48.373;
inline quint8  encodeCoolant(double c)     { return static_cast<quint8>((c + kCoolantOffset) / kCoolantScale); }
inline double  decodeCoolant(quint8 b)     { return kCoolantScale * b - kCoolantOffset; }

// 0x545 oil temp: byte - 48 °C  (TOIL_CAN, DME4 spec — different formula from coolant)
constexpr double kOilTempOffset = 48.0;
inline quint8  encodeOilTemp(double c)     { return static_cast<quint8>(c + kOilTempOffset); }
inline double  decodeOilTemp(quint8 b)     { return static_cast<double>(b) - kOilTempOffset; }

// 0x329 speed: wire unit is km/h, no scaling needed
inline quint16 encodeSpeed(int kmh)     { return static_cast<quint16>(kmh); }
inline int     decodeSpeed(quint16 raw) { return static_cast<int>(raw); }

inline quint8 encodeGear(int gear)  { return static_cast<quint8>(gear < 0 ? 7 : gear); }
inline int    decodeGear(quint8 b)  { return b == 7 ? -1 : static_cast<int>(b); }

} // namespace CanScaling
