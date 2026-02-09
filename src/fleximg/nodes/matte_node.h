#ifndef FLEXIMG_MATTE_NODE_H
#define FLEXIMG_MATTE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/canvas_utils.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// MatteNode - マット合成ノード
// ========================================================================
//
// 3つの入力画像を使ってマット合成（アルファマスク合成）を行います。
// - 入力ポート0: 前景（マスク白部分に表示）
// - 入力ポート1: 背景（マスク黒部分に表示）
// - 入力ポート2: アルファマスク（Alpha8推奨）
// - 出力: 1ポート
//
// 計算式:
//   Output = Foreground × Alpha + Background × (1 - Alpha)
//
// 未接続・範囲外の扱い:
// - 前景/背景: 透明の黒 (0,0,0,0)
// - アルファマスク: alpha=0（全面背景）
//
// 最適化:
// - 処理順序: マスク → 背景 → 前景（マスク結果に応じて前景要求を最適化）
// - マスクが空または全面0の場合: 背景をそのまま返す（早期リターン）
// - マスクの有効範囲スキャン: 左右の0連続領域を除外し、前景要求範囲を縮小
// - ランレングス処理: 同一alpha値の連続区間をまとめて処理
// - alpha=0/255の特殊ケース: memcpy/memsetで高速処理
//
// 使用例:
//   MatteNode matte;
//   foreground >> matte;              // ポート0（前景）
//   background.connectTo(matte, 1);   // ポート1（背景）
//   mask.connectTo(matte, 2);         // ポート2（マスク）
//   matte >> sink;
//

class MatteNode : public Node {
public:
    MatteNode()
    {
        initPorts(3, 1);  // 3入力、1出力
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char *name() const override
    {
        return "MatteNode";
    }

    // getDataRange: 上流データ範囲の和集合を返す
    DataRange getDataRange(const RenderRequest &request) const override;

#if defined(BENCH_M5STACK) || defined(BENCH_NATIVE)
    // ========================================
    // ベンチマーク用公開API
    // ========================================

    // fgあり領域の行処理（ベンチマーク用ラッパー）
    static void benchProcessRowWithFg(uint8_t *d, const uint8_t *m, const uint8_t *s, int pixelCount);

    // fgなし領域の行処理（ベンチマーク用ラッパー）
    static void benchProcessRowNoFg(uint8_t *d, const uint8_t *m, int pixelCount);
#endif

protected:
    int nodeTypeForMetrics() const override
    {
        return NodeType::Matte;
    }

protected:
    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 全上流ノードにPrepareRequestを伝播
    PrepareResponse onPullPrepare(const PrepareRequest &request) override;

    // onPullFinalize: 全上流ノードに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: マット合成処理
    RenderResponse &onPullProcess(const RenderRequest &request) override;

private:
    // ========================================
    // ヘルパー構造体・関数
    // ========================================

    // 入力画像のビュー情報（座標変換済み）
    struct InputView {
        const uint8_t *ptr = nullptr;
        int16_t width = 0, height = 0;
        int32_t stride  = 0;
        int16_t offsetX = 0, offsetY = 0;

        bool valid() const
        {
            return ptr != nullptr;
        }

        // 指定Y座標の行ポインタ（範囲外ならnullptr）
        const uint8_t *rowAt(int_fast16_t y) const
        {
            auto srcY = static_cast<int_fast16_t>(y - offsetY);
            if (static_cast<unsigned>(srcY) >= static_cast<unsigned>(height)) return nullptr;
            return ptr + srcY * stride;
        }

        // RenderResponseから構築
        static InputView from(const RenderResponse &resp, int_fixed outOriginX, int_fixed outOriginY)
        {
            InputView v;
            if (!resp.isValid()) return v;
            ViewPort vp = resp.view();
            v.ptr       = static_cast<const uint8_t *>(vp.data) + vp.y * vp.stride + vp.x * vp.bytesPerPixel();
            v.width     = vp.width;
            v.height    = vp.height;
            v.stride    = vp.stride;
            v.offsetX   = static_cast<int16_t>(from_fixed(resp.origin.x - outOriginX));
            v.offsetY   = static_cast<int16_t>(from_fixed(resp.origin.y - outOriginY));
            return v;
        }
    };

    // マスクの左右0スキップ範囲をスキャン（4バイト単位、アライメント対応）
    // 戻り値: 有効範囲の幅（0なら全面0）
    static int_fast16_t scanMaskZeroRanges(const uint8_t *maskData, int_fast16_t maskWidth, int_fast16_t &outLeftSkip,
                                           int_fast16_t &outRightSkip);

    // ========================================
    // 合成処理
    // ========================================

    // マット合成の実処理（出力には既にbgがコピー済み前提）
    // alpha=0: 何もしない（出力に既にbgがある）
    // alpha=255: fgをコピー
    // 中間alpha: out = out*(1-a) + fg*a
    void applyMatteOverlay(ImageBuffer &output, int_fast16_t outWidth, const InputView &fg, const InputView &mask);

    // ========================================
    // キャッシュ（getDataRange→onPullProcess間で再利用）
    // ========================================
    struct RangeCache {
        Point origin{};          // キャッシュ時のリクエストorigin
        DataRange fgRange{};     // fg のデータ範囲
        DataRange bgRange{};     // bg のデータ範囲
        DataRange maskRange{};   // mask のデータ範囲
        DataRange unionRange{};  // 全体の和集合
        bool valid = false;      // キャッシュ有効フラグ
    };
    mutable RangeCache rangeCache_;

    // 上流データ範囲を計算（キャッシュに保存）
    DataRange calcUpstreamRanges(const RenderRequest &request) const;
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_MATTE_NODE_H
