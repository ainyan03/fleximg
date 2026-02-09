#ifndef FLEXIMG_RENDERER_NODE_H
#define FLEXIMG_RENDERER_NODE_H

#include "../core/format_metrics.h"
#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../core/render_context.h"
#include "../core/types.h"
#include "../image/image_buffer_entry_pool.h"
#include "../image/render_types.h"
#include <algorithm>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RendererNode - パイプライン発火点ノード
// ========================================================================
//
// パイプライン実行の発火点となるノードです。
// - 入力: 1ポート（上流の処理ノード）
// - 出力: 1ポート（下流のSinkNode/DistributorNode）
// - 仮想スクリーンサイズと pivot を持つ（pivot は独立して機能）
// - タイル分割処理を制御
//
// 使用例:
//   SourceNode src;
//   RendererNode renderer;
//   SinkNode sink(output);
//
//   src >> renderer >> sink;
//
//   renderer.setVirtualScreen(320, 240);
//   renderer.setPivotCenter();  // 明示的に設定が必要
//   renderer.setTileConfig({64, 64});
//   renderer.exec();
//

class RendererNode : public Node {
public:
    RendererNode()
    {
        initPorts(1, 1);  // 1入力・1出力
    }

    // ========================================
    // 設定API
    // ========================================

    // 仮想スクリーン設定
    // サイズを指定。pivot は setPivot() または setPivotCenter() で別途設定
    void setVirtualScreen(int_fast16_t width, int_fast16_t height)
    {
        virtualWidth_  = static_cast<int16_t>(width);
        virtualHeight_ = static_cast<int16_t>(height);
    }

    // pivot設定（スクリーン座標でワールド原点の表示位置を指定）
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

    // 中央をpivotに設定（幾何学的中心）
    void setPivotCenter()
    {
        pivotX_ = to_fixed(virtualWidth_) >> 1;
        pivotY_ = to_fixed(virtualHeight_) >> 1;
    }

    // アクセサ
    std::pair<float, float> getPivot() const
    {
        return {fixed_to_float(pivotX_), fixed_to_float(pivotY_)};
    }

    // タイル設定
    void setTileConfig(const TileConfig &config)
    {
        tileConfig_ = config;
    }

    void setTileConfig(int_fast16_t tileWidth, int_fast16_t tileHeight)
    {
        tileConfig_ = TileConfig(tileWidth, tileHeight);
    }

    // アロケータ設定
    // パイプライン内の各ノードがImageBuffer確保時に使用するアロケータを設定
    // nullptrの場合はデフォルトアロケータを使用
    void setAllocator(core::memory::IAllocator *allocator)
    {
        pipelineAllocator_ = allocator;
    }

    // デバッグ用チェッカーボード
    void setDebugCheckerboard(bool enabled)
    {
        debugCheckerboard_ = enabled;
    }

    // デバッグ用DataRange可視化
    // 有効時、getDataRangeの範囲外をマゼンタ、AABB差分を青で塗りつぶし
    void setDebugDataRange(bool enabled)
    {
        debugDataRange_ = enabled;
    }

    // アクセサ
    int virtualWidth() const
    {
        return virtualWidth_;
    }
    int virtualHeight() const
    {
        return virtualHeight_;
    }
    const TileConfig &tileConfig() const
    {
        return tileConfig_;
    }

    const char *name() const override
    {
        return "RendererNode";
    }

    // ========================================
    // 実行API
    // ========================================

    // 簡易API（prepare → execute → finalize）
    // 戻り値: PrepareStatus（Success = 0、エラー = 非0）
    PrepareStatus exec()
    {
        FLEXIMG_METRICS_SCOPE(NodeType::Renderer);

        PrepareStatus result = execPrepare();
        if (result != PrepareStatus::Prepared) {
            // エラー時も状態をリセット
            execFinalize();
            return result;
        }
        execProcess();
        execFinalize();
        return PrepareStatus::Prepared;
    }

    // 詳細API
    // 戻り値: PrepareStatus（Success = 0、エラー = 非0）
    PrepareStatus execPrepare();
    void execProcess();

    void execFinalize()
    {
        // 上流へ終了を伝播（プル型）
        Node *upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullFinalize();
        }

        // 下流へ終了を伝播（プッシュ型）
        Node *downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }

        // エントリプールを一括解放
        entryPool_.releaseAll();
    }

    // パフォーマンス計測結果を取得
    const PerfMetrics &getPerfMetrics() const
    {
        return PerfMetrics::instance();
    }

    void resetPerfMetrics()
    {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().reset();
        FormatMetrics::instance().reset();
#endif
    }

protected:
    // タイル処理（派生クラスでカスタマイズ可能）
    // 注: exec()全体の時間はnodes[NodeType::Renderer]に記録される
    //     各ノードの合計との差分がオーバーヘッド（タイル管理、データ受け渡し等）
    virtual void processTile(int_fast16_t tileX, int_fast16_t tileY)
    {
        RenderRequest request = createTileRequest(tileX, tileY);

        // 上流からプル
        Node *upstream = upstreamNode(0);
        if (!upstream) {
            context_.resetScanlineResources();
            return;
        }

        RenderResponse &result = upstream->pullProcess(request);

        // デバッグ: DataRange可視化
        if (debugDataRange_) {
            applyDataRangeDebug(upstream, request, result);
        }

        // 下流へプッシュ（有効なデータがなくても常に転送）
        Node *downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(result, request);
        }

        // タイル処理完了後にResponseプールをリセット
        context_.resetScanlineResources();
    }

    // デバッグ用: DataRange可視化処理（resultを直接変更）
    void applyDataRangeDebug(Node *upstream, const RenderRequest &request, RenderResponse &result);

private:
    int16_t virtualWidth_  = 0;
    int16_t virtualHeight_ = 0;
    int_fixed pivotX_      = 0;
    int_fixed pivotY_      = 0;
    TileConfig tileConfig_;
    bool debugCheckerboard_                      = false;
    bool debugDataRange_                         = false;
    core::memory::IAllocator *pipelineAllocator_ = nullptr;  // パイプライン用アロケータ
    ImageBufferEntryPool entryPool_;                         // RenderResponse用エントリプール
    RenderContext context_;                                  // レンダリングコンテキスト（allocator + entryPool を統合）

    // タイルサイズ取得
    // 注: パイプライン上のリクエストは必ずスキャンライン（height=1）
    //     これにより各ノードの最適化が可能になる
    int_fast16_t effectiveTileWidth() const
    {
        return tileConfig_.isEnabled() ? tileConfig_.tileWidth : virtualWidth_;
    }

    int_fast16_t effectiveTileHeight() const
    {
        // スキャンライン必須（height=1）
        // TileConfig の tileHeight は無視される
        return 1;
    }

    // タイル数取得
    int_fast16_t calcTileCountX() const
    {
        auto tw = effectiveTileWidth();
        return (tw > 0) ? static_cast<int_fast16_t>((virtualWidth_ + tw - 1) / tw) : 1;
    }

    int_fast16_t calcTileCountY() const
    {
        auto th = effectiveTileHeight();
        return (th > 0) ? static_cast<int_fast16_t>((virtualHeight_ + th - 1) / th) : 1;
    }

    // スクリーン全体のRenderRequestを作成
    RenderRequest createScreenRequest() const
    {
        RenderRequest req;
        req.width  = static_cast<int16_t>(virtualWidth_);
        req.height = static_cast<int16_t>(virtualHeight_);
        // スクリーン左上（座標0,0）のワールド座標
        req.origin.x = -pivotX_;
        req.origin.y = -pivotY_;
        return req;
    }

    // タイル用のRenderRequestを作成
    RenderRequest createTileRequest(int_fast16_t tileX, int_fast16_t tileY) const
    {
        auto tw       = effectiveTileWidth();
        auto th       = effectiveTileHeight();
        auto tileLeft = static_cast<int_fast16_t>(tileX * tw);
        auto tileTop  = static_cast<int_fast16_t>(tileY * th);

        // タイルサイズ（端の処理）
        auto tileW = std::min<int_fast16_t>(tw, virtualWidth_ - tileLeft);
        auto tileH = std::min<int_fast16_t>(th, virtualHeight_ - tileTop);

        RenderRequest req;
        req.width  = static_cast<int16_t>(tileW);
        req.height = static_cast<int16_t>(tileH);
        // タイル左上のワールド座標 = スクリーン座標 - ワールド原点のスクリーン座標
        req.origin.x = to_fixed(tileLeft) - pivotX_;
        req.origin.y = to_fixed(tileTop) - pivotY_;
        return req;
    }
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_RENDERER_NODE_H
