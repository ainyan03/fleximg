/**
 * @file composite_node.inl
 * @brief CompositeNode 実装
 * @see src/fleximg/nodes/composite_node.h
 */

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// CompositeNode - Template Method フック実装
// ============================================================================

PrepareResponse CompositeNode::onPullPrepare(const PrepareRequest &request)
{
    PrepareResponse merged;
    merged.status          = PrepareStatus::Prepared;
    int validUpstreamCount = 0;

    // AABB和集合計算用（基準点からの相対座標）
    float minX = 0, minY = 0, maxX = 0, maxY = 0;

    // 上流に渡すリクエストを作成（localMatrix_ を累積）
    PrepareRequest upstreamRequest = request;
    if (hasLocalTransform()) {
        // 行列合成: request.affineMatrix * localMatrix_
        // AffineNode直列接続と同じ解釈順序
        if (upstreamRequest.hasAffine) {
            upstreamRequest.affineMatrix = upstreamRequest.affineMatrix * localMatrix_;
        } else {
            upstreamRequest.affineMatrix = localMatrix_;
            upstreamRequest.hasAffine    = true;
        }
    }

    // 全上流へ伝播し、結果をマージ（AABB和集合）
    auto numInputs = inputCount();
    for (int_fast16_t i = 0; i < numInputs; ++i) {
        Node *upstream = upstreamNode(i);
        if (upstream) {
            // 各上流に同じリクエストを伝播
            // 注意: アフィン行列は共有されるため、各上流で同じ変換が適用される
            PrepareResponse result = upstream->pullPrepare(upstreamRequest);
            if (!result.ok()) {
                return result;  // エラーを伝播
            }

            // 各結果のAABBをワールド座標に変換
            // origin はバッファ左上のワールド座標
            float left   = fixed_to_float(result.origin.x);
            float top    = fixed_to_float(result.origin.y);
            float right  = left + static_cast<float>(result.width);
            float bottom = top + static_cast<float>(result.height);

            if (validUpstreamCount == 0) {
                // 最初の結果でベースを初期化
                merged.preferredFormat = result.preferredFormat;
                minX                   = left;
                minY                   = top;
                maxX                   = right;
                maxY                   = bottom;
            } else {
                // 和集合（各辺のmin/max）
                if (left < minX) minX = left;
                if (top < minY) minY = top;
                if (right > maxX) maxX = right;
                if (bottom > maxY) maxY = bottom;
            }
            ++validUpstreamCount;
        }
    }

    if (validUpstreamCount > 0) {
        // 和集合結果をPrepareResponseに設定
        // origin はバッファ左上のワールド座標
        merged.width    = static_cast<int16_t>(std::ceil(maxX - minX));
        merged.height   = static_cast<int16_t>(std::ceil(maxY - minY));
        merged.origin.x = float_to_fixed(minX);
        merged.origin.y = float_to_fixed(minY);

        // フォーマット決定:
        // - 上流が1つのみ → パススルー（merged.preferredFormatはそのまま）
        // - 上流が複数 → 合成フォーマットを使用
        if (validUpstreamCount > 1) {
            merged.preferredFormat = PixelFormatIDs::RGBA8_Straight;
        }
    } else {
        // 上流がない場合はサイズ0を返す
        // width/height/originはデフォルト値（0）のまま
    }

    // 準備処理
    RenderRequest screenInfo;
    screenInfo.width  = request.width;
    screenInfo.height = request.height;
    screenInfo.origin = request.origin;
    prepare(screenInfo);

    // getDataRangeキャッシュを無効化（アフィン行列が変わる可能性があるため）
    dataRangeCache_.invalidate();

    return merged;
}

void CompositeNode::onPullFinalize()
{
    finalize();
    auto numInputs = inputCount();
    for (int_fast16_t i = 0; i < numInputs; ++i) {
        Node *upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullFinalize();
        }
    }
}

// getDataRange: 全上流のgetDataRange和集合を返す
// 同一スキャンラインでの重複呼び出しはキャッシュで高速化
DataRange CompositeNode::getDataRange(const RenderRequest &request) const
{
    // キャッシュヒットチェック
    DataRange cached;
    if (dataRangeCache_.tryGet(request, cached)) {
        return cached;
    }

    auto numInputs      = inputCount();
    int_fast16_t startX = request.width;  // 右端で初期化
    int_fast16_t endX   = 0;              // 左端で初期化

    for (int_fast16_t i = 0; i < numInputs; ++i) {
        Node *upstream = upstreamNode(i);
        if (!upstream) continue;

        DataRange range = upstream->getDataRange(request);
        if (!range.hasData()) continue;

        // 和集合を更新
        if (range.startX < startX) startX = range.startX;
        if (range.endX > endX) endX = range.endX;
    }

    // startX >= endX はデータなし
    DataRange result =
        (startX < endX) ? DataRange{static_cast<int16_t>(startX), static_cast<int16_t>(endX)} : DataRange{0, 0};

    // キャッシュ更新
    dataRangeCache_.set(request, result);

    return result;
}

// onPullProcess: 複数の上流から画像を取得してunder合成
// 単一バッファ事前確保方式:
// - getDataRangeで合成範囲を事前計算
// - hintRangeサイズの合成バッファをゼロ初期化で確保
// - 各上流の結果をblendFromで直接書き込み
RenderResponse &CompositeNode::onPullProcess(const RenderRequest &request)
{
    auto numInputs = inputCount();
    if (numInputs == 0) return makeEmptyResponse(request.origin);

    // 1. hintRange取得（キャッシュ付き）
    DataRange hintRange = getDataRange(request);
    if (!hintRange.hasData()) return makeEmptyResponse(request.origin);

    // 2. 合成バッファ確保（ゼロ初期化）
    int16_t hintWidth     = static_cast<int16_t>(hintRange.endX - hintRange.startX);
    Point compositeOrigin = request.origin;
    compositeOrigin.x += to_fixed(hintRange.startX);

    RenderResponse &resp      = context_->acquireResponse();
    ImageBuffer *compositeBuf = resp.createBuffer(hintWidth, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);

    if (!compositeBuf || !compositeBuf->isValid()) {
        return resp;  // alloc失敗
    }
    compositeBuf->setOrigin(compositeOrigin);

    // 3. 各上流を処理
    for (int_fast16_t i = 0; i < numInputs; ++i) {
        Node *upstream = upstreamNode(i);
        if (!upstream) continue;

        RenderResponse &input = upstream->pullProcess(request);
        if (!input.isValid()) {
            context_->releaseResponse(input);
            continue;
        }

        FLEXIMG_METRICS_SCOPE(NodeType::Composite);

        // 上流のバッファをblendFrom
        if (input.hasBuffer()) {
            compositeBuf->blendFrom(input.buffer());
        }

        context_->releaseResponse(input);
    }

    resp.origin = compositeOrigin;
    return resp;
}

}  // namespace FLEXIMG_NAMESPACE
