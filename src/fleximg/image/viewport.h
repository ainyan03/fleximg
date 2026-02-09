#ifndef FLEXIMG_VIEWPORT_H
#define FLEXIMG_VIEWPORT_H

#include "../core/common.h"
#include "../core/types.h"
#include "pixel_format.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ViewPort - 純粋ビュー（軽量POD）
// ========================================================================
//
// 画像データへの軽量なビューです。
// - メモリを所有しない（参照のみ）
// - 最小限のフィールドとメソッドのみ
// - 操作はフリー関数（view_ops名前空間）で提供
//

struct ViewPort {
    void *data             = nullptr;  // 常にバッファ全体の先頭を指す
    PixelFormatID formatID = PixelFormatIDs::RGBA8_Straight;
    int32_t stride         = 0;  // 負値でY軸反転対応
    int16_t width          = 0;
    int16_t height         = 0;
    int16_t x              = 0;  // バッファ内でのビュー左上のX座標
    int16_t y              = 0;  // バッファ内でのビュー左上のY座標

    // デフォルトコンストラクタ
    ViewPort() = default;

    // 直接初期化（引数は最速型、メンバ格納時にキャスト）
    ViewPort(void *d, PixelFormatID fmt, int32_t str, int_fast16_t w, int_fast16_t h)
        : data(d), formatID(fmt), stride(str), width(static_cast<int16_t>(w)), height(static_cast<int16_t>(h))
    {
    }

    // 簡易初期化（strideを自動計算）
    ViewPort(void *d, int_fast16_t w, int_fast16_t h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight)
        : data(d),
          formatID(fmt),
          stride(static_cast<int32_t>(w * fmt->bytesPerPixel)),
          width(static_cast<int16_t>(w)),
          height(static_cast<int16_t>(h))
    {
    }

    // 有効判定
    bool isValid() const
    {
        return data != nullptr && width > 0 && height > 0;
    }

    // ピクセルアドレス取得（strideが負の場合もサポート）
    void *pixelAt(int localX, int localY)
    {
        return static_cast<uint8_t *>(data) + static_cast<int_fast32_t>(this->y + localY) * stride +
               (this->x + localX) * formatID->bytesPerPixel;
    }

    const void *pixelAt(int localX, int localY) const
    {
        return static_cast<const uint8_t *>(data) + static_cast<int_fast32_t>(this->y + localY) * stride +
               (this->x + localX) * formatID->bytesPerPixel;
    }

    // バイト情報
    uint8_t bytesPerPixel() const
    {
        return formatID->bytesPerPixel;
    }
    uint32_t rowBytes() const
    {
        return stride > 0 ? static_cast<uint32_t>(stride)
                          : static_cast<uint32_t>(width) * static_cast<uint32_t>(bytesPerPixel());
    }
};

// ========================================================================
// view_ops - ViewPort操作（フリー関数）
// ========================================================================

namespace view_ops {

// サブビュー作成（引数は最速型、32bitマイコンでのビット切り詰め回避）
inline ViewPort subView(const ViewPort &v, int_fast16_t dx, int_fast16_t dy, int_fast16_t w, int_fast16_t h)
{
    ViewPort result = v;
    result.x        = static_cast<int16_t>(v.x + dx);  // オフセット累積
    result.y        = static_cast<int16_t>(v.y + dy);  // オフセット累積
    result.width    = static_cast<int16_t>(w);
    result.height   = static_cast<int16_t>(h);
    // data, stride, formatID は変更しない（常にバッファ全体を指す）
    return result;
}

// 矩形コピー
void copy(ViewPort &dst, int_fast16_t dstX, int_fast16_t dstY, const ViewPort &src, int_fast16_t srcX,
          int_fast16_t srcY, int_fast16_t width, int_fast16_t height);

// 矩形クリア
void clear(ViewPort &dst, int_fast16_t x, int_fast16_t y, int_fast16_t width, int_fast16_t height);

// ========================================================================
// DDA転写関数
// ========================================================================
//
// アフィン変換等で使用するDDA（Digital Differential Analyzer）方式の
// ピクセル転写関数群。将来のbit-packed format対応を見据え、
// ViewPortから必要情報を取得する設計。
//

// DDA行転写（最近傍補間）
// dst: 出力先メモリ（行バッファ）
// src: ソース全体のViewPort（フォーマット・サイズ情報含む）
// count: 転写ピクセル数
// srcX, srcY: ソース開始座標（Q16.16固定小数点）
// incrX, incrY: 1ピクセルあたりの増分（Q16.16固定小数点）
void copyRowDDA(void *dst, const ViewPort &src, int_fast16_t count, int_fixed srcX, int_fixed srcY, int_fixed incrX,
                int_fixed incrY);

// DDA行転写（バイリニア補間）
// copyQuadDDA → フォーマット変換 → bilinearBlend_RGBA8888 のパイプライン
// copyQuadDDA未対応フォーマットは最近傍にフォールバック
// edgeFadeMask:
// EdgeFadeFlagsの値。フェード有効な辺のみ境界ピクセルのアルファを0化 srcAux:
// パレット情報等（Index8のパレット展開に使用）
void copyRowDDABilinear(void *dst, const ViewPort &src, int_fast16_t count, int_fixed srcX, int_fixed srcY,
                        int_fixed incrX, int_fixed incrY, uint8_t edgeFadeMask, const PixelAuxInfo *srcAux);

// アフィン変換転写（DDA方式）
// 複数行を一括処理する高レベル関数
void affineTransform(ViewPort &dst, const ViewPort &src, int_fixed invTx, int_fixed invTy,
                     const Matrix2x2_fixed &invMatrix, int_fixed rowOffsetX, int_fixed rowOffsetY, int_fixed dxOffsetX,
                     int_fixed dxOffsetY);

// 1chバイリニア補間が使用可能かを判定するヘルパー
// Alpha8, Grayscale8 等の単一チャンネル非インデックスフォーマットに適用
// edgeFadeMask != 0 の場合、Alphaチャンネルのみ対応（値を直接0にできるため）
inline bool canUseSingleChannelBilinear(PixelFormatID formatID, uint8_t edgeFadeMask)
{
    if (!formatID) return false;
    const int bytesPerPixel = formatID->bytesPerPixel;
    return (bytesPerPixel == 1) && (formatID->channelCount == 1) && !formatID->isIndexed &&
           (edgeFadeMask == 0 || formatID->hasAlpha);
}

}  // namespace view_ops

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_VIEWPORT_H
