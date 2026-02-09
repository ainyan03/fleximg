#ifndef FLEXIMG_SINK_NODE_H
#define FLEXIMG_SINK_NODE_H

#include "../core/affine_capability.h"
#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../operations/transform.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// SinkNode - 画像出力ノード（終端）
// ========================================================================
//
// パイプラインの出力端点となるノードです。
// - 入力ポート: 1
// - 出力ポート: 0
// - 外部のViewPortに結果を書き込む
//
// アフィン変換はAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
//
// 座標系と視覚効果:
// - pivot: ワールド原点 (0,0) に対応するバッファ座標 + アフィン回転中心
// - tx/ty: 平行移動（アフィン時は回転の影響を受けない）
// - 変換式（アフィンあり）: src = Inv * (buf - pivot - tx) + pivot
// - 変換式（アフィンなし）: src = buf - pivot - tx
//

class SinkNode : public Node, public AffineCapability {
public:
    // コンストラクタ
    SinkNode()
    {
        initPorts(1, 0);  // 入力1、出力0
    }

    SinkNode(const ViewPort &vp, int_fixed pivotX = 0, int_fixed pivotY = 0)
        : target_(vp), pivotX_(pivotX), pivotY_(pivotY)
    {
        initPorts(1, 0);
    }

    // ターゲット設定
    void setTarget(const ViewPort &vp)
    {
        target_ = vp;
    }

    // pivot 設定（出力バッファ座標、変換の中心点）
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

    // 便利メソッド: ターゲット中央を pivot に設定
    void setPivotCenter()
    {
        pivotX_ = to_fixed(target_.width / 2);
        pivotY_ = to_fixed(target_.height / 2);
    }

    // アクセサ
    const ViewPort &target() const
    {
        return target_;
    }
    ViewPort &target()
    {
        return target_;
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

    // キャンバスサイズ（targetから取得）
    int16_t canvasWidth() const
    {
        return target_.width;
    }
    int16_t canvasHeight() const
    {
        return target_.height;
    }

    const char *name() const override
    {
        return "SinkNode";
    }

protected:
    int nodeTypeForMetrics() const override
    {
        return NodeType::Sink;
    }

protected:
    // ========================================
    // Template Method フック
    // ========================================

    // onPushPrepare: アフィン情報を受け取り、事前計算を行う
    // SinkNodeは終端なので下流への伝播なし、PrepareResponseを返す
    PrepareResponse onPushPrepare(const PrepareRequest &request) override;

    // onPushProcess: タイル単位で呼び出され、出力バッファに書き込み
    // SinkNodeは終端なので下流への伝播なし
    void onPushProcess(RenderResponse &input, const RenderRequest &request) override;

private:
    ViewPort target_;
    int_fixed pivotX_ = 0;  // 変換の中心点X（出力バッファ座標、固定小数点 Q16.16）
    int_fixed pivotY_ = 0;  // 変換の中心点Y（出力バッファ座標、固定小数点 Q16.16）

    // アフィン伝播用メンバ変数（事前計算済み）
    Matrix2x2_fixed invMatrix_;  // 逆行列（固定小数点）
    int_fixed baseTx_ = 0;       // 事前計算済みオフセットX（Q16.16、pivot込み）
    int_fixed baseTy_ = 0;       // 事前計算済みオフセットY（Q16.16、pivot込み）
    bool hasAffine_   = false;   // アフィン変換が伝播されているか

    // アフィン変換付きプッシュ処理
    void pushProcessWithAffine(RenderResponse &input);

    // アフィン変換実装（事前計算済み値を使用）
    void applyAffine(ViewPort &dst, const ViewPort &src, int_fixed srcOriginX, int_fixed srcOriginY);
};

}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_SINK_NODE_H
