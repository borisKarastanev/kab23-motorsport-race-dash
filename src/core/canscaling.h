#pragma once

#include <QtEndian>
#include <QtGlobal>

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

// 0x545 oil temp: byte - 48 °C  (TOIL_CAN, DME4 spec — different formula from coolant)
constexpr double kOilTempOffset = 48.0;
inline quint8  encodeOilTemp(double c)     { return static_cast<quint8>(c + kOilTempOffset); }
inline double  decodeOilTemp(quint8 b)     { return static_cast<double>(b) - kOilTempOffset; }

} // namespace CanScaling
