#ifndef FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
#define FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
extern const PixelFormatDescriptor RGBA8_Straight;
}

namespace PixelFormatIDs {
inline const PixelFormatID RGBA8_Straight = &BuiltinFormats::RGBA8_Straight;
}

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
