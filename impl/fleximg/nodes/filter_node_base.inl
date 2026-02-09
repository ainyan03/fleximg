/**
 * @file filter_node_base.inl
 * @brief FilterNodeBase 実装
 * @see src/fleximg/nodes/filter_node_base.h
 */

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// FilterNodeBase - Template Method フック実装
// ============================================================================

RenderResponse &FilterNodeBase::onPullProcess(const RenderRequest &request)
{
    Node *upstream = upstreamNode(0);
    if (!upstream) return makeEmptyResponse(request.origin);

    int margin             = computeInputMargin();
    RenderRequest inputReq = request.expand(margin);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    // ピクセル効率計測
    auto &metrics = PerfMetrics::instance().nodes[nodeTypeForMetrics()];
    metrics.requestedPixels += static_cast<uint64_t>(inputReq.width) * static_cast<uint64_t>(inputReq.height);
    metrics.usedPixels += static_cast<uint64_t>(request.width) * static_cast<uint64_t>(request.height);
#endif

    RenderResponse &input = upstream->pullProcess(inputReq);
    if (!input.isValid()) return input;

    // process() を呼ぶ（Node基底クラスの設計に沿う）
    return process(input, request);
}

// ============================================================================
// FilterNodeBase - process() 共通実装
// ============================================================================
//
// スキャンライン必須仕様（height=1）前提の共通処理:
// 1. RGBA8_Straight形式に変換
// 2. ラインフィルタ関数を適用
// 3. パフォーマンス計測（デバッグビルド時）
//

RenderResponse &FilterNodeBase::process(RenderResponse &input, const RenderRequest &request)
{
    (void)request;  // スキャンライン必須仕様では未使用
    FLEXIMG_METRICS_SCOPE(nodeTypeForMetrics());

    // フォーマット変換を実行（メトリクス記録付き）
    consolidateIfNeeded(input, PixelFormatIDs::RGBA8_Straight);

    // input.buffer() を直接加工
    ImageBuffer &working = input.buffer();
    ViewPort workingView = working.view();

    // ラインフィルタを適用（height=1前提）
    // ViewPortのx,yオフセットを考慮してpixelAt(0,0)を使用
    uint8_t *row = static_cast<uint8_t *>(workingView.pixelAt(0, 0));
    getFilterFunc()(row, workingView.width, params_);

    // inputをそのまま返す（借用元への変更が反映される）
    return input;
}

}  // namespace FLEXIMG_NAMESPACE
