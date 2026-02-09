#ifndef FLEXIMG_PIXEL_FORMAT_ALPHA8_H
#define FLEXIMG_PIXEL_FORMAT_ALPHA8_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
extern const PixelFormatDescriptor Alpha8;
}

namespace PixelFormatIDs {
inline const PixelFormatID Alpha8 = &BuiltinFormats::Alpha8;
}

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_PIXEL_FORMAT_ALPHA8_H
