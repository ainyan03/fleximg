#ifndef FLEXIMG_PIXEL_FORMAT_RGB888_H
#define FLEXIMG_PIXEL_FORMAT_RGB888_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
extern const PixelFormatDescriptor RGB888;
extern const PixelFormatDescriptor BGR888;
}  // namespace BuiltinFormats

namespace PixelFormatIDs {
inline const PixelFormatID RGB888 = &BuiltinFormats::RGB888;
inline const PixelFormatID BGR888 = &BuiltinFormats::BGR888;
}  // namespace PixelFormatIDs

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_RGB888_H
