/**
 * @file distributor_node.inl
 * @brief DistributorNode 実装
 * @see src/fleximg/nodes/distributor_node.h
 */

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// DistributorNode - Template Method フック実装
// ============================================================================

PrepareResponse DistributorNode::onPushPrepare(const PrepareRequest &request)
{
    // 準備処理
    RenderRequest screenInfo;
    screenInfo.width  = request.width;
    screenInfo.height = request.height;
    screenInfo.origin = request.origin;
    prepare(screenInfo);

    PrepareResponse merged;
    merged.status           = PrepareStatus::Prepared;
    bool hasValidDownstream = false;
    bool formatMismatch     = false;

    // AABB和集合計算用（基準点からの相対座標）
    float minX = 0, minY = 0, maxX = 0, maxY = 0;

    // 下流に渡すリクエストを作成（localMatrix_ を累積）
    PrepareRequest downstreamRequest = request;
    if (hasLocalTransform()) {
        // 行列合成: request.pushAffineMatrix * localMatrix_
        // AffineNode直列接続と同じ解釈順序
        if (downstreamRequest.hasPushAffine) {
            downstreamRequest.pushAffineMatrix = downstreamRequest.pushAffineMatrix * localMatrix_;
        } else {
            downstreamRequest.pushAffineMatrix = localMatrix_;
            downstreamRequest.hasPushAffine    = true;
        }
    }

    // 全下流へ伝播し、結果をマージ（AABB和集合）
    int numOutputs = outputCount();
    for (int i = 0; i < numOutputs; ++i) {
        Node *downstream = downstreamNode(i);
        if (downstream) {
            PrepareResponse result = downstream->pushPrepare(downstreamRequest);
            if (!result.ok()) {
                return result;  // エラーを伝播
            }

            // 各結果のAABBを基準点からの相対座標に変換
            float left   = -fixed_to_float(result.origin.x);
            float top    = -fixed_to_float(result.origin.y);
            float right  = left + static_cast<float>(result.width);
            float bottom = top + static_cast<float>(result.height);

            if (!hasValidDownstream) {
                // 最初の結果でベースを初期化
                merged.preferredFormat = result.preferredFormat;
                minX                   = left;
                minY                   = top;
                maxX                   = right;
                maxY                   = bottom;
                hasValidDownstream     = true;
            } else {
                // 和集合（各辺のmin/max）
                if (left < minX) minX = left;
                if (top < minY) minY = top;
                if (right > maxX) maxX = right;
                if (bottom > maxY) maxY = bottom;
                // フォーマットの差異をチェック
                if (merged.preferredFormat != result.preferredFormat) {
                    formatMismatch = true;
                }
            }
        }
    }

    if (hasValidDownstream) {
        // 和集合結果をPrepareResponseに設定
        merged.width    = static_cast<int16_t>(std::ceil(maxX - minX));
        merged.height   = static_cast<int16_t>(std::ceil(maxY - minY));
        merged.origin.x = float_to_fixed(-minX);
        merged.origin.y = float_to_fixed(-minY);
        // フォーマット決定:
        // - 全下流が同じフォーマット → そのフォーマットを採用
        // - 下流に差異 → RGBA8_Straightを採用（共通の中間フォーマット）
        if (formatMismatch) {
            merged.preferredFormat = PixelFormatIDs::RGBA8_Straight;
        }
    } else {
        // 下流がない場合はサイズ0を返す
        // width/height/originはデフォルト値（0）のまま
    }

    return merged;
}

void DistributorNode::onPushFinalize()
{
    // 全下流へ伝播
    int numOutputs = outputCount();
    for (int i = 0; i < numOutputs; ++i) {
        Node *downstream = downstreamNode(i);
        if (downstream) {
            downstream->pushFinalize();
        }
    }
    finalize();
}

void DistributorNode::onPushProcess(RenderResponse &input, const RenderRequest &request)
{
    // プッシュ型単一入力: 無効なら処理終了
    if (!input.isValid()) {
        return;
    }

    // バッファ準備
    consolidateIfNeeded(input);

    FLEXIMG_METRICS_SCOPE(NodeType::Distributor);

    int numOutputs   = outputCount();
    int validOutputs = 0;

    // 接続されている出力を数える
    for (int i = 0; i < numOutputs; ++i) {
        if (downstreamNode(i)) {
            ++validOutputs;
        }
    }

    if (validOutputs == 0) {
        return;
    }

    // 各出力に参照モードImageBufferを配信
    int processed = 0;
    for (int i = 0; i < numOutputs; ++i) {
        Node *downstream = downstreamNode(i);
        if (!downstream) continue;

        ++processed;

        // 参照モードImageBufferを作成（メモリ解放しない）
        // 最後の出力には元のバッファの参照をそのまま渡す
        if (processed < validOutputs) {
            // 参照モード: ViewPortから新しいImageBufferを作成
            RenderResponse &ref = makeResponse(ImageBuffer(input.buffer().view()), input.origin);
            downstream->pushProcess(ref, request);
        } else {
            // 最後: 元のバッファ参照をそのまま渡す
            downstream->pushProcess(input, request);
        }
    }
}

}  // namespace FLEXIMG_NAMESPACE
