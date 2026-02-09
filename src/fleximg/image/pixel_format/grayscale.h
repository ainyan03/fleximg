#ifndef FLEXIMG_PIXEL_FORMAT_GRAYSCALE_H
#define FLEXIMG_PIXEL_FORMAT_GRAYSCALE_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
// 8-bit Grayscale
extern const PixelFormatDescriptor Grayscale8;

// Bit-packed Grayscale formats
extern const PixelFormatDescriptor Grayscale1_MSB;
extern const PixelFormatDescriptor Grayscale1_LSB;
extern const PixelFormatDescriptor Grayscale2_MSB;
extern const PixelFormatDescriptor Grayscale2_LSB;
extern const PixelFormatDescriptor Grayscale4_MSB;
extern const PixelFormatDescriptor Grayscale4_LSB;
}  // namespace BuiltinFormats

namespace PixelFormatIDs {
inline const PixelFormatID Grayscale8 = &BuiltinFormats::Grayscale8;

inline const PixelFormatID Grayscale1_MSB = &BuiltinFormats::Grayscale1_MSB;
inline const PixelFormatID Grayscale1_LSB = &BuiltinFormats::Grayscale1_LSB;
inline const PixelFormatID Grayscale2_MSB = &BuiltinFormats::Grayscale2_MSB;
inline const PixelFormatID Grayscale2_LSB = &BuiltinFormats::Grayscale2_LSB;
inline const PixelFormatID Grayscale4_MSB = &BuiltinFormats::Grayscale4_MSB;
inline const PixelFormatID Grayscale4_LSB = &BuiltinFormats::Grayscale4_LSB;
}  // namespace PixelFormatIDs

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_GRAYSCALE_H
