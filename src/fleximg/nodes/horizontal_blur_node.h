#ifndef FLEXIMG_HORIZONTAL_BLUR_NODE_H
#define FLEXIMG_HORIZONTAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <algorithm>  // for std::min, std::max
#include <cstring>    // for std::memcpy

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// HorizontalBlurNode - 水平方向ブラーフィルタノード
// ========================================================================
//
// 入力画像に水平方向のボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（0-127、カーネルサイズ = 2 * radius + 1）
// - passes: ブラー適用回数（1-3、デフォルト1）
//
// マルチパス処理:
// - passes=3で3回水平ブラーを適用（ガウシアン近似）
// - 各パスは独立してブラー処理を行う（パイプライン方式）
// - pull型: 上流にマージン付き要求、下流には元サイズで返却
// - push型: 入力を拡張して下流に配布
//
// メモリ消費量:
// - 水平ブラーはスキャンライン処理のため、メモリ消費は少ない
// - 1行分のバッファ: width * 4 bytes 程度
//
// 使用例:
//   HorizontalBlurNode hblur;
//   hblur.setRadius(6);
//   hblur.setPasses(3);  // ガウシアン近似
//   src >> hblur >> sink;
//
// VerticalBlurNodeと組み合わせて2次元ガウシアン近似:
//   src >> hblur(r=6, p=3) >> vblur(r=6, p=3) >> sink;
//

class HorizontalBlurNode : public Node {
public:
    HorizontalBlurNode()
    {
        initPorts(1, 1);
    }

    // ========================================
    // パラメータ設定
    // ========================================

    // パラメータ上限
    static constexpr int kMaxRadius = 127;  // 実用上十分、メモリ消費も許容範囲
    static constexpr int kMaxPasses = 3;    // ガウシアン近似に十分

    void setRadius(int_fast16_t radius)
    {
        radius_ = static_cast<int16_t>((radius < 0) ? 0 : (radius > kMaxRadius) ? kMaxRadius : radius);
    }

    void setPasses(int_fast16_t passes)
    {
        passes_ = static_cast<int16_t>((passes < 1) ? 1 : (passes > kMaxPasses) ? kMaxPasses : passes);
    }

    int16_t radius() const
    {
        return radius_;
    }
    int16_t passes() const
    {
        return passes_;
    }
    int_fast16_t kernelSize() const
    {
        return radius_ * 2 + 1;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char *name() const override
    {
        return "HorizontalBlurNode";
    }

    // getDataRange: 上流データ範囲をブラー分拡張して返す
    DataRange getDataRange(const RenderRequest &request) const override
    {
        if (radius_ == 0 || passes_ == 0) {
            // パススルー時は上流の範囲をそのまま返す
            Node *upstream = upstreamNode(0);
            if (upstream) {
                return upstream->getDataRange(request);
            }
            return DataRange();
        }

        // 上流への拡張リクエストを作成（左方向に拡大）
        auto totalMargin = static_cast<int_fast16_t>(radius_ * passes_);
        RenderRequest inputReq;
        inputReq.width    = static_cast<int16_t>(request.width + totalMargin * 2);
        inputReq.height   = 1;
        inputReq.origin.x = request.origin.x - to_fixed(totalMargin);
        inputReq.origin.y = request.origin.y;

        Node *upstream = upstreamNode(0);
        if (!upstream) {
            return DataRange();
        }

        // 上流のデータ範囲を取得
        DataRange upstreamRange = upstream->getDataRange(inputReq);
        if (!upstreamRange.hasData()) {
            return DataRange();
        }

        // ブラー処理後の範囲を計算
        // upstreamRangeはinputReq座標系 → request座標系への変換: X - totalMargin
        // さらにブラー処理による両側拡張: -totalMargin / +totalMargin
        // 結果: startX - 2*totalMargin, endX
        int16_t blurredStartX = static_cast<int16_t>(upstreamRange.startX - totalMargin * 2);
        int16_t blurredEndX   = static_cast<int16_t>(upstreamRange.endX);

        // request範囲にクランプ
        if (blurredStartX < 0) blurredStartX = 0;
        if (blurredEndX > request.width) blurredEndX = request.width;

        if (blurredStartX >= blurredEndX) {
            return DataRange();
        }

        return DataRange{blurredStartX, blurredEndX};
    }

protected:
    int nodeTypeForMetrics() const override
    {
        return NodeType::HorizontalBlur;
    }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: AABBをX方向に拡張
    PrepareResponse onPullPrepare(const PrepareRequest &request) override;

    // onPullProcess: 水平ブラー処理
    RenderResponse &onPullProcess(const RenderRequest &request) override;

    // onPushProcess: 水平ブラー処理（push型）
    void onPushProcess(RenderResponse &input, const RenderRequest &request) override;

private:
    int16_t radius_ = 5;
    int16_t passes_ = 1;  // 1-3の範囲、デフォルト1

    // 水平方向ブラー処理（共通）
    void applyHorizontalBlur(const ViewPort &srcView, int_fast16_t inputOffset, ImageBuffer &output);

    // ブラー済みピクセルを書き込み
    void writeBlurredPixel(uint8_t *row, int_fast16_t x, uint32_t sumR, uint32_t sumG, uint32_t sumB, uint32_t sumA)
    {
        auto off    = static_cast<int_fast16_t>(x * 4);
        uint32_t ks = static_cast<uint32_t>(kernelSize());
        if (sumA > 0) {
            row[off]     = static_cast<uint8_t>(sumR / sumA);
            row[off + 1] = static_cast<uint8_t>(sumG / sumA);
            row[off + 2] = static_cast<uint8_t>(sumB / sumA);
            row[off + 3] = static_cast<uint8_t>(sumA / ks);
        } else {
            row[off] = row[off + 1] = row[off + 2] = row[off + 3] = 0;
        }
    }
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_HORIZONTAL_BLUR_NODE_H
