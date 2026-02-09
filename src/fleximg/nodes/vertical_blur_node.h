#ifndef FLEXIMG_VERTICAL_BLUR_NODE_H
#define FLEXIMG_VERTICAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <algorithm>
#include <cstdint>  // for int32_t, uint32_t
#include <cstring>
#include <vector>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// VerticalBlurNode - 垂直方向ブラーフィルタノード（スキャンライン対応）
// ========================================================================
//
// 入力画像に垂直方向のボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（0-127、カーネルサイズ = 2 * radius + 1）
// - passes: ブラー適用回数（1-3、デフォルト1）
//
// マルチパス処理（パイプライン方式）:
// - passes=3で3回垂直ブラーを適用（ガウシアン近似）
// - 各パスが独立したステージとして処理され、境界処理も独立に行われる
// - 「3パス×1ノード」と「1パス×3ノード直列」が同等の結果を得る
//
// メモリ消費量（概算）:
// - 各ステージ: (radius * 2 + 1) * width * 4 bytes + width * 16 bytes（列合計）
// - 例: radius=50, passes=3, width=640 → 約500KB
// - 例: radius=127, passes=3, width=2048 → 約4MB
//
// スキャンライン処理:
// - prepare()でキャッシュを確保
// - pullProcess()で行キャッシュと列合計を使用したスライディングウィンドウ処理
// - finalize()でキャッシュを破棄
//
// 使用例:
//   VerticalBlurNode vblur;
//   vblur.setRadius(6);
//   vblur.setPasses(3);  // ガウシアン近似
//   src >> vblur >> sink;
//
// HorizontalBlurNodeと組み合わせて2次元ガウシアン近似:
//   src >> hblur(r=6, p=3) >> vblur(r=6, p=3) >> sink;
//

class VerticalBlurNode : public Node {
public:
    VerticalBlurNode()
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
    int_fast16_t totalKernelSize() const
    {
        return radius_ * 2 * passes_ + 1;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char *name() const override
    {
        return "VerticalBlurNode";
    }

    // getDataRange: 上下radius*passes行の上流DataRange和集合を返す
    DataRange getDataRange(const RenderRequest &request) const override;

    // 準備・終了処理（pull型用）
    void prepare(const RenderRequest &screenInfo) override;
    void finalize() override;

    // Template Method フック
    PrepareResponse onPullPrepare(const PrepareRequest &request) override;
    PrepareResponse onPushPrepare(const PrepareRequest &request) override;
    void onPushProcess(RenderResponse &input, const RenderRequest &request) override;
    void onPushFinalize() override;

protected:
    int nodeTypeForMetrics() const override
    {
        return NodeType::VerticalBlur;
    }
    RenderResponse &onPullProcess(const RenderRequest &request) override;

private:
    int16_t radius_ = 5;
    int16_t passes_ = 1;  // 1-3の範囲、デフォルト1

    // スクリーン情報
    int16_t screenWidth_  = 0;
    int16_t screenHeight_ = 0;
    Point screenOrigin_;

    // ========================================
    // パイプラインステージ構造体
    // ========================================
    // 各ステージが独立したキャッシュと列合計を持つ
    // passes=3の場合、3つのステージがパイプライン接続される
    struct BlurStage {
        std::vector<ImageBuffer> rowCache;    // radius*2+1 行のキャッシュ
        std::vector<int_fixed> rowOriginX;    // 各キャッシュ行のorigin.x（push型用）
        std::vector<DataRange> rowDataRange;  // 各キャッシュ行の有効範囲
        std::vector<uint32_t> colSumR;        // 列合計（R×A）
        std::vector<uint32_t> colSumG;        // 列合計（G×A）
        std::vector<uint32_t> colSumB;        // 列合計（B×A）
        std::vector<uint32_t> colSumA;        // 列合計（A）
        int32_t currentY = 0;                 // 現在のY座標（pull型用）
        bool cacheReady  = false;             // キャッシュ初期化済みフラグ

        // push型用の状態
        int32_t pushInputY  = 0;  // 入力行カウント
        int32_t pushOutputY = 0;  // 出力行カウント

        void clear()
        {
            rowCache.clear();
            rowOriginX.clear();
            rowDataRange.clear();
            colSumR.clear();
            colSumG.clear();
            colSumB.clear();
            colSumA.clear();
            currentY    = 0;
            cacheReady  = false;
            pushInputY  = 0;
            pushOutputY = 0;
        }
    };

    // パイプラインステージ（passes個、passes=1でもstages_[0]を使用）
    std::vector<BlurStage> stages_;
    int16_t cacheWidth_        = 0;
    int_fixed cacheOriginX_    = 0;      // キャッシュの基準X座標（pull型用）
    int_fixed upstreamOriginX_ = 0;      // 上流pullProcessのorigin.x（radius=0と同じ出力用）
    bool upstreamOriginXSet_   = false;  // upstreamOriginX_が設定済みかどうか

    // 上流のY範囲（getDataRangeでのクエリY座標クランプ用）
    int_fixed sourceOriginY_ = 0;  // 上流のorigin.y（拡張前）
    int16_t sourceHeight_    = 0;  // 上流の高さ（拡張前）

    // push型処理用の状態
    int32_t pushInputY_         = 0;
    int32_t pushOutputY_        = 0;
    int16_t pushInputWidth_     = 0;
    int16_t pushInputHeight_    = 0;
    int16_t pushOutputHeight_   = 0;
    int_fixed baseOriginX_      = 0;  // 基準origin.x（pushPrepareで設定）
    int_fixed pushInputOriginY_ = 0;
    int_fixed lastInputOriginY_ = 0;

    // getDataRange/pullProcess 間のキャッシュ
    struct DataRangeCache {
        Point origin   = {INT32_MIN, INT32_MIN};  // キャッシュキー（無効値で初期化）
        int16_t startX = 0;
        int16_t endX   = 0;
    };
    mutable DataRangeCache rangeCache_;

    // 内部実装（宣言のみ）
    RenderResponse &pullProcessPipeline(Node *upstream, const RenderRequest &request);
    void updateStageCache(int_fast16_t stageIndex, Node *upstream, const RenderRequest &request, int_fast16_t newY);
    void fetchRowToStageCache(BlurStage &stage, Node *upstream, const RenderRequest &request, int_fast16_t srcY,
                              int_fast16_t cacheIndex);
    void fetchRowFromPrevStage(int_fast16_t stageIndex, Node *upstream, const RenderRequest &request, int_fast16_t srcY,
                               int_fast16_t cacheIndex);
    void updateStageColSum(BlurStage &stage, int_fast16_t cacheIndex, bool add);
    void computeStageOutputRow(BlurStage &stage, ImageBuffer &output, int_fast16_t width);
    void initializeStage(BlurStage &stage, int_fast16_t width);
    void initializeStages(int_fast16_t width);
    void propagatePipelineStages();
    void emitBlurredLinePipeline();
    void storeInputRowToStageCache(BlurStage &stage, const ImageBuffer &input, int_fast16_t cacheIndex,
                                   int_fast16_t xOffset = 0);
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_VERTICAL_BLUR_NODE_H
