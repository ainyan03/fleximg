#ifndef FLEXIMG_AFFINE_NODE_H
#define FLEXIMG_AFFINE_NODE_H

#include "../core/affine_capability.h"
#include "../core/node.h"
#include "../core/perf_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// AffineNode - アフィン変換ノード
// ========================================================================
//
// 入力画像に対してアフィン変換（回転・拡縮・平行移動）を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 特徴:
// - アフィン行列を保持し、SourceNode/SinkNodeに伝播する
// - 実際のDDA処理はSourceNode（プル型）またはSinkNode（プッシュ型）で実行
// - 複数のAffineNodeがある場合は行列を合成して伝播
//
// セッターはAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
//
// 使用例:
//   AffineNode affine;
//   affine.setRotation(0.5f);
//   src >> affine >> sink;
//

class AffineNode : public Node, public AffineCapability {
public:
    AffineNode()
    {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char *name() const override
    {
        return "AffineNode";
    }

protected:
    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: アフィン行列を上流に伝播し、SourceNodeで一括実行
    PrepareResponse onPullPrepare(const PrepareRequest &request) override;

    // onPushPrepare: アフィン行列を下流に伝播し、SinkNodeで一括実行
    PrepareResponse onPushPrepare(const PrepareRequest &request) override;

    // onPullProcess: AffineNodeは行列を保持するのみ、パススルー
    RenderResponse &onPullProcess(const RenderRequest &request) override;

    // onPushProcess: AffineNodeは行列を保持するのみ、パススルー
    void onPushProcess(RenderResponse &input, const RenderRequest &request) override;

    int nodeTypeForMetrics() const override
    {
        return NodeType::Affine;
    }
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_AFFINE_NODE_H
