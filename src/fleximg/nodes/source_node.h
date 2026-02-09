#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../core/affine_capability.h"
#include "../core/data_range_cache.h"
#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/viewport.h"
#include "../operations/transform.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <cstdio>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// InterpolationMode - 補間モード
// ========================================================================

enum class InterpolationMode {
    Nearest,  // 最近傍補間（デフォルト）
    Bilinear  // バイリニア補間（RGBA8888のみ対応）
};

// ========================================================================
// SourceNode - 画像入力ノード（終端）
// ========================================================================
//
// パイプラインの入力端点となるノードです。
// - 入力ポート: 0
// - 出力ポート: 1
// - 外部のViewPortを参照
//
// アフィン変換はAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
//
// setPosition() は setTranslation() のエイリアスとして提供（後方互換）
//

class SourceNode : public Node, public AffineCapability {
public:
    // コンストラクタ
    SourceNode()
    {
        initPorts(0, 1);  // 入力0、出力1
    }

    SourceNode(const ViewPort &vp, int_fixed pivotX = 0, int_fixed pivotY = 0)
        : source_(vp), pivotX_(pivotX), pivotY_(pivotY)
    {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort &vp)
    {
        source_  = vp;
        palette_ = PaletteData();
    }
    void setSource(const ViewPort &vp, const PaletteData &palette)
    {
        source_  = vp;
        palette_ = palette;
    }

    // 基準点設定（pivot: 画像内のアンカーポイント）
    void setPivot(int_fixed x, int_fixed y)
    {
        pivotX_ = x;
        pivotY_ = y;
    }
    void setPivot(float x, float y)
    {
        pivotX_ = float_to_fixed(x);
        pivotY_ = float_to_fixed(y);
    }

    // アクセサ
    const ViewPort &source() const
    {
        return source_;
    }
    int_fixed pivotX() const
    {
        return pivotX_;
    }
    int_fixed pivotY() const
    {
        return pivotY_;
    }
    std::pair<float, float> getPivot() const
    {
        return {fixed_to_float(pivotX_), fixed_to_float(pivotY_)};
    }

    // ユーザー向けAPI: position（setTranslation のエイリアス、後方互換）
    void setPosition(float x, float y)
    {
        setTranslation(x, y);
    }
    std::pair<float, float> getPosition() const
    {
        return {localMatrix_.tx, localMatrix_.ty};
    }

    // カラーキー設定（アルファなしフォーマットで特定色を透明化）
    void setColorKey(uint32_t colorKeyRGBA8, uint32_t replaceRGBA8 = 0)
    {
        colorKeyRGBA8_   = colorKeyRGBA8;
        colorKeyReplace_ = replaceRGBA8;
    }
    void clearColorKey()
    {
        colorKeyRGBA8_   = 0;
        colorKeyReplace_ = 0;
    }

    // 補間モード設定
    void setInterpolationMode(InterpolationMode mode)
    {
        interpolationMode_ = mode;
    }
    InterpolationMode interpolationMode() const
    {
        return interpolationMode_;
    }

    // エッジフェードアウト設定（バイリニア補間時のみ有効）
    // フェード有効な辺では出力範囲が0.5ピクセル拡張され、境界がなめらかに透明化
    // フェード無効な辺では出力範囲はNearestと同じ、境界ピクセルはクランプ
    void setEdgeFade(uint8_t flags)
    {
        edgeFadeFlags_ = flags;
    }
    uint8_t edgeFade() const
    {
        return edgeFadeFlags_;
    }

    const char *name() const override
    {
        return "SourceNode";
    }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: アフィン情報を受け取り、事前計算を行う
    // SourceNodeは終端なので上流への伝播なし、PrepareResponseを返す
    PrepareResponse onPullPrepare(const PrepareRequest &request) override;

    // onPullProcess: ソース画像のスキャンラインを返す
    // SourceNodeは入力がないため、上流を呼び出さずに直接処理
    RenderResponse &onPullProcess(const RenderRequest &request) override;

    // getDataRange: スキャンライン単位の正確なデータ範囲を返す
    // アフィン変換がある場合、calcScanlineRangeで厳密な有効範囲を計算
    // AABB上限が必要な場合は getDataRangeBounds() を使用
    DataRange getDataRange(const RenderRequest &request) const override;

private:
    ViewPort source_;
    PaletteData palette_;   // パレット情報（インデックスフォーマット用、非所有）
    int_fixed pivotX_ = 0;  // 画像内の基準点X（pivot: 回転・配置の中心、固定小数点 Q16.16）
    int_fixed pivotY_ = 0;  // 画像内の基準点Y（pivot: 回転・配置の中心、固定小数点 Q16.16）
    // 注: 配置位置は localMatrix_.tx/ty で管理（AffineCapability から継承）
    InterpolationMode interpolationMode_ = InterpolationMode::Nearest;
    uint8_t edgeFadeFlags_               = EdgeFade_All;  // デフォルト: 全辺フェードアウト有効
    uint32_t colorKeyRGBA8_              = 0;             // カラーキー比較値（RGBA8、alpha込み）
    uint32_t colorKeyReplace_            = 0;             // カラーキー差し替え値（通常は透明黒0）

    // アフィン伝播用メンバ変数（事前計算済み）
    AffinePrecomputed affine_;  // 逆行列・ピクセル中心オフセット
    bool hasAffine_   = false;  // アフィン変換が伝播されているか
    bool useBilinear_ = false;  // バイリニア補間を使用するか（事前計算結果）

    // フォーマット交渉（下流からの希望フォーマット）
    PixelFormatID preferredFormat_ = PixelFormatIDs::RGBA8_Straight;

    // LovyanGFX方式の範囲計算用事前計算値
    int_fixed xs1_ = 0, xs2_ = 0;      // X方向の範囲境界（invAに依存）
    int_fixed ys1_ = 0, ys2_ = 0;      // Y方向の範囲境界（invCに依存）
    int_fixed fpWidth_           = 0;  // ソース幅（Q16.16固定小数点）
    int_fixed fpHeight_          = 0;  // ソース高さ（Q16.16固定小数点）
    int_fixed baseTxWithOffsets_ = 0;  // 事前計算統合: invTx + srcPivot + rowOffset + dxOffset
    int_fixed baseTyWithOffsets_ = 0;  // 事前計算統合: invTy + srcPivot + rowOffset + dxOffset

    // Prepare時のorigin（Process時の差分計算用）
    int_fixed prepareOriginX_ = 0;
    int_fixed prepareOriginY_ = 0;

    // getDataRangeキャッシュ（同一スキャンラインでの重複計算を回避）
    // NinePatchSourceNode等から同一requestで複数回呼ばれるケースに対応
    mutable core::DataRangeCache dataRangeCache_;

    // スキャンライン有効範囲を計算（pullProcessWithAffineで使用）
    // 戻り値: true=有効範囲あり, false=有効範囲なし
    // baseXWithHalf/baseYWithHalf はオプショナル出力（nullptrなら出力しない）
    bool calcScanlineRange(const RenderRequest &request, int32_t &dxStart, int32_t &dxEnd,
                           int32_t *baseXWithHalf = nullptr, int32_t *baseYWithHalf = nullptr) const;

    // アフィン変換付きプル処理（スキャンライン専用）
    RenderResponse &pullProcessWithAffine(const RenderRequest &request);
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_SOURCE_NODE_H
