#ifndef FLEXIMG_FILTER_NODE_BASE_H
#define FLEXIMG_FILTER_NODE_BASE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../operations/filters.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// FilterNodeBase - フィルタノード基底クラス
// ========================================================================
//
// フィルタ系ノードの共通基底クラスです。
// - 入力: 1ポート
// - 出力: 1ポート
// - スキャンライン必須仕様（height=1）前提で動作
//
// 派生クラスの実装:
//   - getFilterFunc() でフィルタ関数を返す
//   - params_ にパラメータを設定
//   - nodeTypeForMetrics() でメトリクス用ノードタイプを返す
//
// 派生クラスの実装例:
//   class BrightnessNode : public FilterNodeBase {
//   public:
//       void setAmount(float v) { params_.value1 = v; }
//       float amount() const { return params_.value1; }
//   protected:
//       filters::LineFilterFunc getFilterFunc() const override {
//           return &filters::brightness_line;
//       }
//       int nodeTypeForMetrics() const override { return NodeType::Brightness;
//       } const char* name() const override { return "BrightnessNode"; }
//   };
//

class FilterNodeBase : public Node {
public:
    FilterNodeBase()
    {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char *name() const override
    {
        return "FilterNodeBase";
    }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullProcess: マージン追加とメトリクス記録を行い、process() に委譲
    RenderResponse &onPullProcess(const RenderRequest &request) override;

protected:
    // ========================================
    // 派生クラスがオーバーライドするフック
    // ========================================

    /// ラインフィルタ関数を返す（派生クラスで実装）
    virtual filters::LineFilterFunc getFilterFunc() const = 0;

    /// 入力マージン（ブラー等で拡大が必要な場合にオーバーライド）
    virtual int computeInputMargin() const
    {
        return 0;
    }

    /// メトリクス用ノードタイプ（派生クラスで実装）
    int nodeTypeForMetrics() const override = 0;

    // process() 共通実装
    // スキャンライン必須仕様（height=1）前提の共通処理
    RenderResponse &process(RenderResponse &input, const RenderRequest &request) override;

    // ========================================
    // パラメータ（派生クラスからアクセス可能）
    // ========================================

    filters::LineFilterParams params_;
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_FILTER_NODE_BASE_H
