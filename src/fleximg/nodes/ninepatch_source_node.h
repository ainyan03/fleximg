#ifndef FLEXIMG_NINEPATCH_SOURCE_NODE_H
#define FLEXIMG_NINEPATCH_SOURCE_NODE_H

#include "../core/affine_capability.h"
#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/viewport.h"
#include "../operations/canvas_utils.h"
#include "source_node.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// NinePatchSourceNode - 9patch画像ソースノード
// ========================================================================
//
// Android の 9patch 画像に相当する、伸縮可能な画像ソースノード。
// 画像を9つの区画に分割し、角は固定サイズ、辺と中央は伸縮する。
//
// 9分割された領域:
// ┌────────┬─────────────────┬────────┐
// │ [0]    │      [1]        │   [2]  │  固定高さ
// │ 固定   │    横伸縮       │  固定  │
// ├────────┼─────────────────┼────────┤
// │ [3]    │      [4]        │   [5]  │  可変高さ
// │ 縦伸縮 │   両方向伸縮    │ 縦伸縮 │
// ├────────┼─────────────────┼────────┤
// │ [6]    │      [7]        │   [8]  │  固定高さ
// │ 固定   │    横伸縮       │  固定  │
// └────────┴─────────────────┴────────┘
//   固定幅    可変幅           固定幅
//
// - 入力ポート: 0（終端ノード）
// - 出力ポート: 1
//

class NinePatchSourceNode : public Node, public AffineCapability {
public:
    // コンストラクタ
    NinePatchSourceNode()
    {
        initPorts(0, 1);  // 入力0、出力1（終端ノード）
    }

    // ========================================
    // 初期化メソッド
    // ========================================

    // 通常画像 + 境界座標を明示指定（上級者向け/内部用）
    // left/top/right/bottom: 各角の固定サイズ（ピクセル）
    void setupWithBounds(const ViewPort &image, int_fast16_t left, int_fast16_t top, int_fast16_t right,
                         int_fast16_t bottom)
    {
        source_    = image;
        srcLeft_   = static_cast<int16_t>(left);
        srcTop_    = static_cast<int16_t>(top);
        srcRight_  = static_cast<int16_t>(right);
        srcBottom_ = static_cast<int16_t>(bottom);
        // クリッピングなしの初期状態
        effectiveSrcLeft_   = static_cast<int16_t>(left);
        effectiveSrcRight_  = static_cast<int16_t>(right);
        effectiveSrcTop_    = static_cast<int16_t>(top);
        effectiveSrcBottom_ = static_cast<int16_t>(bottom);
        sourceValid_        = image.isValid();
        geometryValid_      = false;

        // 各区画のソースサイズを計算
        calcSrcPatchSizes();
    }

    // 9patch互換画像（外周1pxがメタデータ）を渡す（メインAPI）
    // 外周1pxを解析して境界座標を自動取得、内部画像を抽出
    void setupFromNinePatch(const ViewPort &ninePatchImage)
    {
        if (!ninePatchImage.isValid() || ninePatchImage.width < 3 || ninePatchImage.height < 3) {
            sourceValid_ = false;
            return;
        }

        // 黒ピクセル判定ラムダ（RGBA8_Straight: R=0, G=0, B=0, A>0）
        auto isBlack = [&](int x, int y) -> bool {
            const uint8_t *pixel = static_cast<const uint8_t *>(ninePatchImage.pixelAt(x, y));
            if (!pixel) return false;
            return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] > 0;
        };

        // 内部画像（外周1pxを除く）を抽出
        ViewPort innerImage =
            view_ops::subView(ninePatchImage, 1, 1, ninePatchImage.width - 2, ninePatchImage.height - 2);

        // 上辺（y=0）のメタデータを解析 → 横方向の伸縮領域
        int_fast16_t stretchXStart = -1, stretchXEnd = -1;
        for (int_fast16_t x = 1; x < ninePatchImage.width - 1; x++) {
            if (isBlack(x, 0)) {
                if (stretchXStart < 0) stretchXStart = x - 1;  // 外周を除いた座標
                stretchXEnd = x - 1;
            }
        }

        // 左辺（x=0）のメタデータを解析 → 縦方向の伸縮領域
        int_fast16_t stretchYStart = -1, stretchYEnd = -1;
        for (int_fast16_t y = 1; y < ninePatchImage.height - 1; y++) {
            if (isBlack(0, y)) {
                if (stretchYStart < 0) stretchYStart = y - 1;  // 外周を除いた座標
                stretchYEnd = y - 1;
            }
        }

        // 境界座標を計算
        auto left   = static_cast<int_fast16_t>((stretchXStart >= 0) ? (stretchXStart) : 0);
        auto right  = static_cast<int_fast16_t>((stretchXEnd >= 0) ? (innerImage.width - 1 - stretchXEnd) : 0);
        auto top    = static_cast<int_fast16_t>((stretchYStart >= 0) ? (stretchYStart) : 0);
        auto bottom = static_cast<int_fast16_t>((stretchYEnd >= 0) ? (innerImage.height - 1 - stretchYEnd) : 0);

        // setupWithBounds を呼び出し
        setupWithBounds(innerImage, left, top, right, bottom);
    }

    // 出力サイズ設定（小数対応）
    void setOutputSize(float width, float height)
    {
        if (outputWidth_ != width || outputHeight_ != height) {
            outputWidth_   = width;
            outputHeight_  = height;
            geometryValid_ = false;
        }
    }

    // 基準点設定（pivot: 画像内のアンカーポイント、デフォルトは左上 (0,0)）
    void setPivot(int_fixed x, int_fixed y)
    {
        if (pivotX_ != x || pivotY_ != y) {
            pivotX_        = x;
            pivotY_        = y;
            geometryValid_ = false;  // アフィン行列の再計算が必要
        }
    }

    // 配置位置設定（アフィン行列のtx/tyに加算）
    void setPosition(float x, float y)
    {
        if (positionX_ != x || positionY_ != y) {
            positionX_     = x;
            positionY_     = y;
            geometryValid_ = false;  // アフィン行列の再計算が必要
        }
    }

    // 補間モード設定（内部の全SourceNodeに適用）
    void setInterpolationMode(InterpolationMode mode)
    {
        if (interpolationMode_ != mode) {
            interpolationMode_ = mode;
            geometryValid_     = false;  // ソースビュー再設定が必要
        }
        for (int i = 0; i < 9; i++) {
            patches_[i].setInterpolationMode(mode);
        }
    }

    // ========================================
    // アクセサ
    // ========================================

    float outputWidth() const
    {
        return outputWidth_;
    }
    float outputHeight() const
    {
        return outputHeight_;
    }
    int_fixed pivotX() const
    {
        return pivotX_;
    }
    int_fixed pivotY() const
    {
        return pivotY_;
    }

    // 境界座標（読み取り用）
    int_fast16_t srcLeft() const
    {
        return srcLeft_;
    }
    int_fast16_t srcTop() const
    {
        return srcTop_;
    }
    int_fast16_t srcRight() const
    {
        return srcRight_;
    }
    int_fast16_t srcBottom() const
    {
        return srcBottom_;
    }

    const char *name() const override
    {
        return "NinePatchSourceNode";
    }
    int nodeTypeForMetrics() const override
    {
        return NodeType::NinePatch;
    }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 各区画のSourceNodeにPrepareRequestを伝播
    PrepareResponse onPullPrepare(const PrepareRequest &request) override;

    // onPullFinalize: 各区画のSourceNodeに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: 全9区画を処理して合成
    RenderResponse &onPullProcess(const RenderRequest &request) override;

    // getDataRange: 全パッチのデータ範囲の和集合を返す
    DataRange getDataRange(const RenderRequest &request) const override;

private:
    // 内部SourceNode（9区画）
    SourceNode patches_[9];

    // 元画像
    ViewPort source_;
    bool sourceValid_ = false;

    // 区画境界（ソース座標）
    int16_t srcLeft_   = 0;  // 左端からの固定幅
    int16_t srcTop_    = 0;  // 上端からの固定高さ
    int16_t srcRight_  = 0;  // 右端からの固定幅
    int16_t srcBottom_ = 0;  // 下端からの固定高さ

    // クリッピング適用後の固定部サイズ（出力サイズが固定部合計より小さい場合に使用）
    int16_t effectiveSrcLeft_   = 0;
    int16_t effectiveSrcRight_  = 0;
    int16_t effectiveSrcTop_    = 0;
    int16_t effectiveSrcBottom_ = 0;

    // 出力サイズ（小数対応）
    float outputWidth_  = 0.0f;
    float outputHeight_ = 0.0f;

    // 基準点（pivot: 回転・配置の中心、出力座標系）
    int_fixed pivotX_ = 0;
    int_fixed pivotY_ = 0;

    // 配置位置（アフィン行列のtx/tyに加算）
    float positionX_ = 0.0f;
    float positionY_ = 0.0f;

    // 補間モード
    InterpolationMode interpolationMode_ = InterpolationMode::Nearest;

    // ジオメトリ計算結果（小数対応）
    bool geometryValid_    = false;
    float patchWidths_[3]  = {0, 0, 0};  // [左固定, 中央伸縮, 右固定]
    float patchHeights_[3] = {0, 0, 0};  // [上固定, 中央伸縮, 下固定]
    float patchOffsetX_[3] = {0, 0, 0};  // 各列の出力X開始位置
    float patchOffsetY_[3] = {0, 0, 0};  // 各行の出力Y開始位置

    // ソース画像内の各区画のサイズ
    int16_t srcPatchW_[3] = {0, 0, 0};  // 各列のソース幅
    int16_t srcPatchH_[3] = {0, 0, 0};  // 各行のソース高さ

    // 各区画のスケール行列（伸縮用）
    AffineMatrix patchScales_[9];
    bool patchNeedsAffine_[9] = {false};  // スケールが1.0でない場合true

    // ========================================
    // 内部メソッド
    // ========================================

    int_fast16_t getPatchIndex(int_fast16_t col, int_fast16_t row) const
    {
        return row * 3 + col;
    }

    // 各区画のソースサイズを計算（初期化時に呼び出し）
    void calcSrcPatchSizes();

    // 1軸方向のクリッピング計算（横/縦共通）
    void calcAxisClipping(float outputSize, int_fast16_t srcFixed0, int_fast16_t srcFixed2, float &outWidth0,
                          float &outWidth1, float &outWidth2, int16_t &effSrc0, int16_t &effSrc2);

    // 出力サイズ変更時にジオメトリを再計算
    void updatePatchGeometry();
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_NINEPATCH_SOURCE_NODE_H
