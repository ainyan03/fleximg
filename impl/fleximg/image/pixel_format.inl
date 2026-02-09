/**
 * @file pixel_format.inl
 * @brief PixelFormat 実装集約
 * @see src/fleximg/image/pixel_format.h
 */

// pixel_format.h 自身の実装（lut8toN テンプレート）

namespace FLEXIMG_NAMESPACE {
namespace pixel_format {
namespace detail {

template <typename T>
void lut8toN(T *d, const uint8_t *s, size_t pixelCount, const T *lut)
{
    while (pixelCount & 3) {
        auto v0 = s[0];
        ++s;
        auto l0 = lut[v0];
        --pixelCount;
        d[0] = l0;
        ++d;
    }
    pixelCount >>= 2;
    if (pixelCount == 0) return;
    do {
        auto v0 = s[0];
        auto v1 = s[1];
        auto v2 = s[2];
        auto v3 = s[3];
        s += 4;
        auto l0 = lut[v0];
        auto l1 = lut[v1];
        auto l2 = lut[v2];
        auto l3 = lut[v3];
        d[0]    = l0;
        d[1]    = l1;
        d[2]    = l2;
        d[3]    = l3;
        d += 4;
    } while (--pixelCount);
}

// 明示的インスタンス化（非inlineを維持）
template void lut8toN<uint16_t>(uint16_t *, const uint8_t *, size_t, const uint16_t *);
template void lut8toN<uint32_t>(uint32_t *, const uint8_t *, size_t, const uint32_t *);

}  // namespace detail
}  // namespace pixel_format
}  // namespace FLEXIMG_NAMESPACE

// 各フォーマット実装
#include "pixel_format/alpha8.inl"
#include "pixel_format/grayscale.inl"
#include "pixel_format/index.inl"
#include "pixel_format/rgb332.inl"
#include "pixel_format/rgb565.inl"
#include "pixel_format/rgb888.inl"
#include "pixel_format/rgba8_straight.inl"

// DDA関数（index.inl の bit_packed_detail 定義後に必要）
#include "pixel_format/dda.inl"

// FormatConverter 実装
#include "pixel_format/format_converter.inl"
