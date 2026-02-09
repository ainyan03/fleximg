#ifndef FLEXIMG_PIXEL_FORMAT_INDEX_H
#define FLEXIMG_PIXEL_FORMAT_INDEX_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言（Indexフォーマット全般）
// ========================================================================
//
// このファイルは以下のインデックスフォーマットを定義:
// - Index1_MSB/LSB: 1ビット/ピクセル（bit-packed）
// - Index2_MSB/LSB: 2ビット/ピクセル（bit-packed）
// - Index4_MSB/LSB: 4ビット/ピクセル（bit-packed）
// - Index8: 8ビット/ピクセル
//

namespace BuiltinFormats {
// Bit-packed Index formats
extern const PixelFormatDescriptor Index1_MSB;
extern const PixelFormatDescriptor Index1_LSB;
extern const PixelFormatDescriptor Index2_MSB;
extern const PixelFormatDescriptor Index2_LSB;
extern const PixelFormatDescriptor Index4_MSB;
extern const PixelFormatDescriptor Index4_LSB;

// 8-bit Index format
extern const PixelFormatDescriptor Index8;
}  // namespace BuiltinFormats

namespace PixelFormatIDs {
inline const PixelFormatID Index1_MSB = &BuiltinFormats::Index1_MSB;
inline const PixelFormatID Index1_LSB = &BuiltinFormats::Index1_LSB;
inline const PixelFormatID Index2_MSB = &BuiltinFormats::Index2_MSB;
inline const PixelFormatID Index2_LSB = &BuiltinFormats::Index2_LSB;
inline const PixelFormatID Index4_MSB = &BuiltinFormats::Index4_MSB;
inline const PixelFormatID Index4_LSB = &BuiltinFormats::Index4_LSB;

inline const PixelFormatID Index8 = &BuiltinFormats::Index8;
}  // namespace PixelFormatIDs

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_INDEX_H
