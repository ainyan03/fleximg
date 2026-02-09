/**
 * @file node.inl
 * @brief Node クラスの実装
 * @see src/fleximg/core/node.h
 */

namespace FLEXIMG_NAMESPACE {
namespace core {

// ============================================================================
// Node - ヘルパーメソッド実装
// ============================================================================

// 循環参照チェック（pullPrepare/pushPrepare共通）
// prepareResponse_.status を参照・更新する
// 戻り値: true=成功, false=エラー
// shouldContinue: true=処理継続, false=スキップ（Prepared）またはエラー
bool Node::checkPrepareStatus(bool &shouldContinue)
{
    PrepareStatus &status = prepareResponse_.status;
    if (status == PrepareStatus::Preparing) {
        status         = PrepareStatus::CycleError;
        shouldContinue = false;
        return false;  // 循環参照検出
    }
    if (status == PrepareStatus::Prepared) {
        shouldContinue = false;
        return true;  // 成功（DAG共有、スキップ）
    }
    if (status == PrepareStatus::CycleError) {
        shouldContinue = false;
        return false;  // 既にエラー状態
    }
    status         = PrepareStatus::Preparing;
    shouldContinue = true;
    return true;  // 成功（処理継続）
}

// フォーマット変換ヘルパー（メトリクス記録付き）
// 参照モードから所有モードに変わった場合、ノード別統計に記録
// allocator()を使用してバッファを確保する
ImageBuffer Node::convertFormat(ImageBuffer &&buffer, PixelFormatID target, FormatConversion mode,
                                const FormatConverter *converter)
{
    bool wasOwning = buffer.ownsMemory();

    // 参照モードの場合、ノードのallocator()を新バッファ用に渡す
    // 注: setAllocator()で参照バッファのallocatorを変更すると、
    //     デストラクタが非所有メモリを解放しようとするバグがあるため、
    //     toFormat()のallocパラメータで安全に渡す
    core::memory::IAllocator *newAlloc = wasOwning ? nullptr : allocator();

    ImageBuffer result = std::move(buffer).toFormat(target, mode, newAlloc, converter);

    // 参照→所有モードへの変換時にメトリクス記録
    if (!wasOwning && result.ownsMemory()) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[nodeTypeForMetrics()].recordAlloc(result.totalBytes(), result.width(),
                                                                        result.height());
#endif
    }
    return result;
}

// 派生クラス用：ポート初期化
void Node::initPorts(int_fast16_t inputCount, int_fast16_t outputCount)
{
    inputs_.resize(static_cast<size_t>(inputCount));
    outputs_.resize(static_cast<size_t>(outputCount));
    for (int_fast16_t i = 0; i < inputCount; ++i) {
        inputs_[static_cast<size_t>(i)] = Port(this, i);
    }
    for (int_fast16_t i = 0; i < outputCount; ++i) {
        outputs_[static_cast<size_t>(i)] = Port(this, i);
    }
}

// バッファ整理ヘルパー
// フォーマット変換を行う
void Node::consolidateIfNeeded(RenderResponse &input, PixelFormatID format)
{
    if (input.empty()) {
        return;
    }

    // フォーマット変換が必要な場合
    // convertFormat()経由でメトリクス記録を維持
    if (format != nullptr) {
        PixelFormatID srcFormat = input.buffer().formatID();
        if (srcFormat != format) {
            ImageBuffer converted = convertFormat(std::move(input.buffer()), format);
            input.replaceBuffer(std::move(converted));
        }
    }

    // バッファoriginをresponse.originに同期
    input.origin = input.buffer().origin();
}

// RenderResponse構築ヘルパー
// RenderContext経由でResponseを取得し、バッファにワールド座標originを設定して追加
RenderResponse &Node::makeResponse(ImageBuffer &&buf, Point origin)
{
    FLEXIMG_ASSERT(context_ != nullptr, "RenderContext required for makeResponse");
    RenderResponse &resp = context_->acquireResponse();
    if (buf.isValid()) {
        buf.setOrigin(origin);
        resp.addBuffer(std::move(buf));
    }
    resp.origin = origin;
    return resp;
}

// 空のRenderResponseを構築
RenderResponse &Node::makeEmptyResponse(Point origin)
{
    FLEXIMG_ASSERT(context_ != nullptr, "RenderContext required for makeEmptyResponse");
    RenderResponse &resp = context_->acquireResponse();
    resp.origin          = origin;
    return resp;
}

}  // namespace core
}  // namespace FLEXIMG_NAMESPACE
