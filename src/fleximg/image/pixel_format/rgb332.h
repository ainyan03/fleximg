#ifndef FLEXIMG_PIXEL_FORMAT_RGB332_H
#define FLEXIMG_PIXEL_FORMAT_RGB332_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
extern const PixelFormatDescriptor RGB332;
}

namespace PixelFormatIDs {
inline const PixelFormatID RGB332 = &BuiltinFormats::RGB332;
}

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_RGB332_H
