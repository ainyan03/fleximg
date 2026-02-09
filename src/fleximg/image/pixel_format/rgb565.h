#ifndef FLEXIMG_PIXEL_FORMAT_RGB565_H
#define FLEXIMG_PIXEL_FORMAT_RGB565_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
extern const PixelFormatDescriptor RGB565_LE;
extern const PixelFormatDescriptor RGB565_BE;
}  // namespace BuiltinFormats

namespace PixelFormatIDs {
inline const PixelFormatID RGB565_LE = &BuiltinFormats::RGB565_LE;
inline const PixelFormatID RGB565_BE = &BuiltinFormats::RGB565_BE;
}  // namespace PixelFormatIDs

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_RGB565_H
