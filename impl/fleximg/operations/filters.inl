/**
 * @file filters.inl
 * @brief ラインフィルタ関数の実装
 * @see src/fleximg/operations/filters.h
 */

#include <algorithm>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// ラインフィルタ関数（スキャンライン処理用）
// ========================================================================

void brightness_line(uint8_t *pixels, int_fast16_t count, const LineFilterParams &params)
{
    auto adjustment = static_cast<int_fast16_t>(params.value1 * 255.0f);

    for (int_fast16_t x = 0; x < count; x++) {
        int_fast16_t pixelOffset = x * 4;
        // RGB各チャンネルに明るさ調整を適用
        for (int_fast16_t c = 0; c < 3; c++) {
            auto value = static_cast<int_fast16_t>(pixels[pixelOffset + c] + adjustment);
            pixels[pixelOffset + c] =
                static_cast<uint8_t>(std::max<int_fast16_t>(0, std::min<int_fast16_t>(255, value)));
        }
        // Alphaはそのまま維持
    }
}

void grayscale_line(uint8_t *pixels, int_fast16_t count, const LineFilterParams &params)
{
    (void)params;  // 将来の拡張用に引数は維持

    for (int_fast16_t x = 0; x < count; x++) {
        int_fast16_t pixelOffset = x * 4;
        // グレースケール変換（平均法）
        uint8_t gray            = static_cast<uint8_t>((static_cast<uint16_t>(pixels[pixelOffset]) +
                                             static_cast<uint16_t>(pixels[pixelOffset + 1]) +
                                             static_cast<uint16_t>(pixels[pixelOffset + 2])) /
                                                       3);
        pixels[pixelOffset]     = gray;  // R
        pixels[pixelOffset + 1] = gray;  // G
        pixels[pixelOffset + 2] = gray;  // B
                                         // Alphaはそのまま維持
    }
}

void alpha_line(uint8_t *pixels, int_fast16_t count, const LineFilterParams &params)
{
    uint32_t alphaScale = static_cast<uint32_t>(params.value1 * 256.0f);

    for (int_fast16_t x = 0; x < count; x++) {
        int_fast16_t pixelOffset = x * 4;
        // RGBはそのまま、Alphaのみスケール
        uint32_t a              = pixels[pixelOffset + 3];
        pixels[pixelOffset + 3] = static_cast<uint8_t>((a * alphaScale) >> 8);
    }
}

}  // namespace filters
}  // namespace FLEXIMG_NAMESPACE
