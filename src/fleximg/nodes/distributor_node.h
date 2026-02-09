#ifndef FLEXIMG_DISTRIBUTOR_NODE_H
#define FLEXIMG_DISTRIBUTOR_NODE_H

#include "../core/affine_capability.h"
#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// DistributorNode - 分配ノード
// ========================================================================
//
// 1つの入力画像を複数の出力に分配します。
// - 入力: 1ポート
// - 出力: コンストラクタで指定（デフォルト1）
//
// CompositeNode（N入力・1出力）と対称的な構造です。
//
// メモリ管理:
// - 下流には参照モードImageBuffer（ownsMemory()==false）を渡す
// - 下流ノードが変更を加えたい場合はコピーを作成する
// - ImageLibrary → SourceNode の関係と対称的
//
// アフィン変換はAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
// - 設定した変換は全下流ノードに伝播される
//
// 使用例:
//   DistributorNode distributor(2);  // 2出力
//   distributor.setScale(0.5f, 0.5f); // 分配先全てに縮小を適用
//   renderer >> distributor;
//   distributor.connectTo(sink1, 0, 0);  // 出力0 → sink1
//   distributor.connectTo(sink2, 0, 1);  // 出力1 → sink2
//

class DistributorNode : public Node, public AffineCapability {
public:
    explicit DistributorNode(int outputCount = 1)
    {
        initPorts(1, static_cast<int_fast16_t>(outputCount));  // 入力1、出力N
    }

    // ========================================
    // 出力管理（CompositeNode::setInputCount と対称）
    // ========================================

    // 出力数を変更（既存接続は維持）
    void setOutputCount(int_fast16_t count)
    {
        if (count < 1) count = 1;
        outputs_.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (outputs_[static_cast<size_t>(i)].owner == nullptr) {
                outputs_[static_cast<size_t>(i)] = core::Port(this, i);
            }
        }
    }

    int outputCount() const
    {
        return static_cast<int>(outputs_.size());
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char *name() const override
    {
        return "DistributorNode";
    }
    int nodeTypeForMetrics() const override
    {
        return NodeType::Distributor;
    }

    // ========================================
    // Template Method フック
    // ========================================

    // onPushPrepare: 全下流ノードにPrepareRequestを伝播
    PrepareResponse onPushPrepare(const PrepareRequest &request) override;

    // onPushFinalize: 全下流ノードに終了を伝播
    void onPushFinalize() override;

    // onPushProcess: 全出力に参照モードで配信
    void onPushProcess(RenderResponse &input, const RenderRequest &request) override;
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_DISTRIBUTOR_NODE_H
