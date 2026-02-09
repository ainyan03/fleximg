/**
 * @file viewport.inl
 * @brief ViewPort 操作関数の実装
 * @see src/fleximg/image/viewport.h
 */

#include "../../../src/fleximg/operations/transform.h"
#include <algorithm>
#include <cstring>

namespace FLEXIMG_NAMESPACE {
namespace view_ops {

void copy(ViewPort &dst, int_fast16_t dstX, int_fast16_t dstY, const ViewPort &src, int_fast16_t srcX,
          int_fast16_t srcY, int_fast16_t width, int_fast16_t height)
{
    if (!dst.isValid() || !src.isValid()) return;

    // クリッピング
    if (srcX < 0) {
        dstX -= srcX;
        width += srcX;
        srcX = 0;
    }
    if (srcY < 0) {
        dstY -= srcY;
        height += srcY;
        srcY = 0;
    }
    if (dstX < 0) {
        srcX -= dstX;
        width += dstX;
        dstX = 0;
    }
    if (dstY < 0) {
        srcY -= dstY;
        height += dstY;
        dstY = 0;
    }
    width  = std::min<int_fast16_t>(width, std::min<int_fast16_t>(src.width - srcX, dst.width - dstX));
    height = std::min<int_fast16_t>(height, std::min<int_fast16_t>(src.height - srcY, dst.height - dstY));
    if (width <= 0 || height <= 0) return;

    // view_ops::copy は同一フォーマット間の矩形コピー専用。
    // 異フォーマット間変換は resolveConverter / convertFormat
    // を直接使用すること。
    FLEXIMG_ASSERT(src.formatID == dst.formatID,
                   "view_ops::copy requires matching formats; use convertFormat "
                   "for conversion");

    size_t bytesPerPixel = static_cast<size_t>(dst.bytesPerPixel());
    for (int_fast16_t y = 0; y < height; ++y) {
        const uint8_t *srcRow = static_cast<const uint8_t *>(src.pixelAt(srcX, srcY + y));
        uint8_t *dstRow       = static_cast<uint8_t *>(dst.pixelAt(dstX, dstY + y));
        std::memcpy(dstRow, srcRow, static_cast<size_t>(width) * bytesPerPixel);
    }
}

void clear(ViewPort &dst, int_fast16_t x, int_fast16_t y, int_fast16_t width, int_fast16_t height)
{
    if (!dst.isValid()) return;

    size_t bytesPerPixel = static_cast<size_t>(dst.bytesPerPixel());
    for (int_fast16_t row = 0; row < height; ++row) {
        auto dy = static_cast<int_fast16_t>(y + row);
        if (dy < 0 || dy >= dst.height) continue;
        uint8_t *dstRow = static_cast<uint8_t *>(dst.pixelAt(x, dy));
        std::memset(dstRow, 0, static_cast<size_t>(width) * bytesPerPixel);
    }
}

// ============================================================================
// DDA転写関数 - 実装
// ============================================================================

}  // namespace view_ops

namespace view_ops {

void copyRowDDA(void *dst, const ViewPort &src, int_fast16_t count, int_fixed srcX, int_fixed srcY, int_fixed incrX,
                int_fixed incrY)
{
    if (!src.isValid() || count <= 0) return;

    // ViewPortのオフセットを固定小数点に変換して加算
    int_fixed offsetX = static_cast<int_fixed>(src.x) << INT_FIXED_SHIFT;
    int_fixed offsetY = static_cast<int_fixed>(src.y) << INT_FIXED_SHIFT;

    // DDAParam を構築（copyRowDDAでは srcWidth/srcHeight/weights は使用しない）
    DDAParam param = {src.stride,     0,     0,
                      srcX + offsetX,  // オフセット加算
                      srcY + offsetY,  // オフセット加算
                      incrX,          incrY, nullptr, nullptr};

    // フォーマットの関数ポインタを呼び出し
    if (src.formatID && src.formatID->copyRowDDA) {
        src.formatID->copyRowDDA(static_cast<uint8_t *>(dst), static_cast<const uint8_t *>(src.data), count, &param);
    }
}

// ============================================================================
// バイリニア補間関数（RGBA8888固定）
// ============================================================================
//
// copyQuadDDAで抽出した4ピクセルデータからバイリニア補間を実行する。
// 入力: quadPixels = [p00,p10,p01,p11] × count（各ピクセル4bytes、RGBA8888）
//       境界外ピクセルは呼び出し前にゼロ埋めされていること
// 出力: dst = 補間結果 × count（各4bytes、RGBA8888）
// 注意: 両ポインタは4バイトアライメントが必要

__attribute__((noinline)) static void bilinearBlend_RGBA8888(uint32_t *__restrict__ dst,
                                                             const uint32_t *__restrict__ quadPixels,
                                                             const BilinearWeightXY *__restrict__ weightsXY, int count)
{
    for (int i = 0; i < count; ++i) {
        uint_fast8_t fy = weightsXY->fy;
        uint_fast8_t fx = weightsXY->fx;
        ++weightsXY;

        uint32_t f         = static_cast<uint32_t>(fx) * ((256 - fy) | (static_cast<uint32_t>(fy) << 16));
        uint8_t q11f       = static_cast<uint8_t>(f >> 24);
        uint8_t q10f       = static_cast<uint8_t>(f >> 8);
        uint8_t q01f       = static_cast<uint8_t>(((256 - fx) * fy) >> 8);
        uint_fast16_t q00f = 256 - (q11f + q01f + q10f);

        // 4点を32bitでロード（境界外ピクセルは事前にゼロ埋め済み）
        uint32_t q00 = quadPixels[0];
        uint32_t q10 = quadPixels[1];
        uint32_t q01 = quadPixels[2];
        uint32_t q11 = quadPixels[3];
        quadPixels += 4;

        // R,B（偶数バイト位置）をマスク
        uint32_t result_rb = q00f * (q00 & 0xFF00FF);
        // G,A（奇数バイト位置）をシフト＆マスク
        uint32_t result_ga = q00f * ((q00 >> 8) & 0xFF00FF);

        result_rb += q10f * (q10 & 0xFF00FF);
        result_ga += q10f * ((q10 >> 8) & 0xFF00FF);

        result_rb += q01f * (q01 & 0xFF00FF);
        result_ga += q01f * ((q01 >> 8) & 0xFF00FF);

        result_rb += q11f * (q11 & 0xFF00FF);
        result_ga += q11f * ((q11 >> 8) & 0xFF00FF);

        // 結果を出力（リトルエンディアン: G,Aは上位バイトが正しい位置に来る）
        auto dstBytes = reinterpret_cast<uint8_t *>(dst);
        *dst          = result_ga;
        dstBytes[0]   = static_cast<uint8_t>(result_rb >> 8);   // R
        dstBytes[2]   = static_cast<uint8_t>(result_rb >> 24);  // B

        ++dst;
    }
}

// ============================================================================
// バイリニア補間関数（1チャンネル固定: Alpha8/Grayscale8用）
// ============================================================================
//
// copyQuadDDAで抽出した4ピクセルデータからバイリニア補間を実行する。
// 入力: quadPixels = [p00,p10,p01,p11] × count（各ピクセル1byte）
//       境界外ピクセルは呼び出し前にゼロ埋めされていること
// 出力: dst = 補間結果 × count（各1byte）

__attribute__((noinline)) static void bilinearBlend_1ch(uint8_t *__restrict__ dst,
                                                        const uint8_t *__restrict__ quadPixels,
                                                        const BilinearWeightXY *__restrict__ weightsXY, int count)
{
    for (int i = 0; i < count; ++i) {
        // 重み計算
        uint32_t q4      = *reinterpret_cast<const uint32_t *>(quadPixels);
        uint_fast32_t fy = weightsXY->fy;
        uint_fast32_t fx = weightsXY->fx;
        ++weightsXY;
        uint32_t left     = (q4 & 0x00FF00FF);               // 左側ピクセルの抽出
        uint32_t right    = (q4 >> 8) & 0x00FF00FF;          // 右側ピクセルの抽出
        uint32_t tb       = left * (256 - fx) + right * fx;  // 左右補間
        uint32_t top      = (tb & 0x0000FFFF) * (256 - fy);
        uint32_t bottom   = (tb >> 16) * fy;
        uint32_t combined = top + bottom;
        uint16_t result   = static_cast<uint16_t>(combined >> 16);
        quadPixels += 4;
        dst[i] = static_cast<uint8_t>(result);
    }
}

// ============================================================================
// copyRowDDABilinear
// ============================================================================
//
// 処理フロー（チャンクループ）:
//   a. copyQuadDDA: 4ピクセル抽出 + edgeFlags生成
//   b. convertFormat: フォーマット変換（RGBA8_Straight以外の場合）
//   c. edgeFlags適用: 境界ピクセルのアルファを0化
//   d. bilinearBlend_RGBA8888: バイリニア補間
//

void copyRowDDABilinear(void *dst, const ViewPort &src, int_fast16_t count, int_fixed srcX, int_fixed srcY,
                        int_fixed incrX, int_fixed incrY, uint8_t edgeFadeMask, const PixelAuxInfo *srcAux)
{
    if (!src.isValid() || count <= 0) return;

    // copyQuadDDA未対応フォーマットは最近傍フォールバック
    if (!src.formatID || !src.formatID->copyQuadDDA) {
        copyRowDDA(dst, src, count, srcX, srcY, incrX, incrY);
        return;
    }

    // ========================================================================
    // 1chパス: Alpha8/Grayscale8等の単一チャンネルフォーマット向け高速パス
    // フォーマット変換不要、メモリ使用量1/4、演算量約1/4
    // ========================================================================
    if (canUseSingleChannelBilinear(src.formatID, edgeFadeMask)) {
        constexpr int CHUNK_SIZE = 64;

        // 1ch用一時バッファ（RGBA8パスの1/4サイズ）
        uint8_t quadBuffer1ch[CHUNK_SIZE * 4];   // 256 bytes（1byte × 4 × 64）
        BilinearWeightXY weightsXY[CHUNK_SIZE];  // 128 bytes
        uint8_t edgeFlagsChunk[CHUNK_SIZE];      // 64 bytes

        uint8_t *dstPtr        = static_cast<uint8_t *>(dst);
        const uint8_t *srcData = static_cast<const uint8_t *>(src.data);

        // ViewPortのオフセットを固定小数点に変換して加算
        int_fixed offsetX = static_cast<int_fixed>(src.x) << INT_FIXED_SHIFT;
        int_fixed offsetY = static_cast<int_fixed>(src.y) << INT_FIXED_SHIFT;

        DDAParam param = {src.stride,     src.width, src.height,
                          srcX + offsetX,  // オフセット加算
                          srcY + offsetY,  // オフセット加算
                          incrX,          incrY,     weightsXY,  edgeFlagsChunk};

        for (int_fast16_t offset = 0; offset < count; offset += CHUNK_SIZE) {
            int_fast16_t chunk = (count - offset < CHUNK_SIZE) ? (count - offset) : CHUNK_SIZE;

            // 4ピクセル抽出（1 byte/pixel: copyQuadDDA出力がそのまま使える）
            src.formatID->copyQuadDDA(quadBuffer1ch, srcData, chunk, &param);

            // 境界ピクセルの値を0化（Alpha8のエッジフェード）
            if (edgeFadeMask) {
                for (int_fast16_t i = 0; i < chunk; ++i) {
                    uint8_t flags = edgeFlagsChunk[i] & edgeFadeMask;
                    if (flags) {
                        uint8_t *q = &quadBuffer1ch[i * 4];
                        if (flags & (EdgeFade_Left | EdgeFade_Top)) {
                            q[0] = 0;
                        }
                        if (flags & (EdgeFade_Right | EdgeFade_Top)) {
                            q[1] = 0;
                        }
                        if (flags & (EdgeFade_Left | EdgeFade_Bottom)) {
                            q[2] = 0;
                        }
                        if (flags & (EdgeFade_Right | EdgeFade_Bottom)) {
                            q[3] = 0;
                        }
                    }
                }
            }

            // 1chバイリニア補間
            bilinearBlend_1ch(dstPtr, quadBuffer1ch, weightsXY, chunk);

            dstPtr += chunk;
            param.srcX += incrX * chunk;
            param.srcY += incrY * chunk;
        }
        return;
    }

    // ========================================================================
    // RGBA8パス: 通常のマルチチャンネルフォーマット
    // ========================================================================

    // チャンク処理用定数
    constexpr int CHUNK_SIZE = 64;
    constexpr int RGBA8_BPP  = 4;

    // 一時バッファ（スタック確保）
    // copyQuadDDA出力とconvertFormat出力を共有（末尾詰め配置でin-place変換可能）
    uint32_t quadBuffer[CHUNK_SIZE * 4];     // 1024 bytes（RGBA8888 × 4 × 64）
    BilinearWeightXY weightsXY[CHUNK_SIZE];  // 128 bytes (2 * 64)
    uint8_t edgeFlagsChunk[CHUNK_SIZE];      // 64 bytes（チャンク用）

    // 元フォーマットのBytesPerPixel（末尾詰め配置用）
    // bit-packedの場合、copyQuadDDAはIndex8形式で出力するため、1バイトとする
    const int srcBytesPerPixel = (src.formatID->pixelsPerUnit > 1) ? 1  // bit-packed: copyQuadDDAがIndex8で出力
                                                                   : src.formatID->bytesPerPixel;  // 通常フォーマット

    // フォーマット変換が必要な場合、ループ外で一度だけresolveConverter呼び出し
    // bit-packedの場合、copyQuadDDAの出力はIndex8形式なので、Index8→RGBA8の変換を使う
    FormatConverter converter;
    PixelFormatID converterSrcFormat = (src.formatID->pixelsPerUnit > 1) ? PixelFormatIDs::Index8 : src.formatID;
    if (converterSrcFormat != PixelFormatIDs::RGBA8_Straight) {
        converter = resolveConverter(converterSrcFormat, PixelFormatIDs::RGBA8_Straight, srcAux);
    }

    uint32_t *dstPtr       = static_cast<uint32_t *>(dst);
    const uint8_t *srcData = static_cast<const uint8_t *>(src.data);

    // ViewPortのオフセットを固定小数点に変換して加算
    int_fixed offsetX = static_cast<int_fixed>(src.x) << INT_FIXED_SHIFT;
    int_fixed offsetY = static_cast<int_fixed>(src.y) << INT_FIXED_SHIFT;

    // DDAParam を構築
    DDAParam param = {src.stride,     src.width, src.height,
                      srcX + offsetX,  // オフセット加算
                      srcY + offsetY,  // オフセット加算
                      incrX,          incrY,     weightsXY,  edgeFlagsChunk};

    for (int_fast16_t offset = 0; offset < count; offset += CHUNK_SIZE) {
        int_fast16_t chunk = (count - offset < CHUNK_SIZE) ? (count - offset) : CHUNK_SIZE;

        // 4ピクセル抽出 + edgeFlags生成（末尾詰め配置でin-place変換可能）
        int srcQuadSize = srcBytesPerPixel * 4 * chunk;
        int dstQuadSize = RGBA8_BPP * 4 * chunk;
        auto quadPtr    = reinterpret_cast<uint8_t *>(quadBuffer) + (dstQuadSize - srcQuadSize);
        src.formatID->copyQuadDDA(quadPtr, srcData, chunk, &param);

        // フォーマット変換（必要な場合、in-place）
        if (converter) {
            converter(quadBuffer, quadPtr, static_cast<size_t>(chunk) * 4);
        }
        uint32_t *quadRGBA = quadBuffer;

        // 境界ピクセルのアルファ0化（edgeFlagsに基づく）
        if (edgeFadeMask) {
            auto quad = reinterpret_cast<uint8_t *>(quadRGBA) + 3;
            for (int_fast16_t i = 0; i < chunk; ++i) {
                uint8_t flags = edgeFlagsChunk[i] & edgeFadeMask;
                if (flags) {
                    if (flags & (EdgeFade_Left | EdgeFade_Top)) {
                        quad[0] = 0;
                    }
                    if (flags & (EdgeFade_Right | EdgeFade_Top)) {
                        quad[4] = 0;
                    }
                    if (flags & (EdgeFade_Left | EdgeFade_Bottom)) {
                        quad[8] = 0;
                    }
                    if (flags & (EdgeFade_Right | EdgeFade_Bottom)) {
                        quad[12] = 0;
                    }
                }
                quad += 4 * 4;
            }
        }

        // バイリニア補間
        bilinearBlend_RGBA8888(dstPtr, quadRGBA, weightsXY, chunk);

        // 次のチャンクへ
        dstPtr += chunk;
        param.srcX += incrX * chunk;
        param.srcY += incrY * chunk;
    }
}

void affineTransform(ViewPort &dst, const ViewPort &src, int_fixed invTx, int_fixed invTy,
                     const Matrix2x2_fixed &invMatrix, int_fixed rowOffsetX, int_fixed rowOffsetY, int_fixed dxOffsetX,
                     int_fixed dxOffsetY)
{
    if (!dst.isValid() || !src.isValid()) return;
    if (!invMatrix.valid) return;

    const int outW = dst.width;
    const int outH = dst.height;

    const int_fixed incrX = invMatrix.a;
    const int_fixed incrY = invMatrix.c;
    const int_fixed invB  = invMatrix.b;
    const int_fixed invD  = invMatrix.d;

    for (int dy = 0; dy < outH; dy++) {
        int_fixed rowBaseX = invB * dy + invTx + rowOffsetX;
        int_fixed rowBaseY = invD * dy + invTy + rowOffsetY;

        auto [xStart, xEnd] = transform::calcValidRange(incrX, rowBaseX, src.width, outW);
        auto [yStart, yEnd] = transform::calcValidRange(incrY, rowBaseY, src.height, outW);
        int dxStart         = std::max({0, xStart, yStart});
        int dxEnd           = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        int_fixed srcX     = incrX * dxStart + rowBaseX + dxOffsetX;
        int_fixed srcY     = incrY * dxStart + rowBaseY + dxOffsetY;
        int_fast16_t count = static_cast<int_fast16_t>(dxEnd - dxStart + 1);

        void *dstRow = dst.pixelAt(dxStart, dy);

        copyRowDDA(dstRow, src, count, srcX, srcY, incrX, incrY);
    }
}

}  // namespace view_ops
}  // namespace FLEXIMG_NAMESPACE
