/**
 * @file affine_node.inl
 * @brief AffineNode 実装
 * @see src/fleximg/nodes/affine_node.h
 */

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// AffineNode - Template Method フック実装
// ============================================================================

// 複数のAffineNodeがある場合は行列を合成する
PrepareResponse AffineNode::onPullPrepare(const PrepareRequest &request)
{
    // 上流に渡すためのコピーを作成し、自身の行列を累積
    PrepareRequest upstreamRequest = request;
    if (upstreamRequest.hasAffine) {
        // 既存の行列（下流側）に自身の行列（上流側）を後から掛ける
        upstreamRequest.affineMatrix = upstreamRequest.affineMatrix * localMatrix_;
    } else {
        upstreamRequest.affineMatrix = localMatrix_;
        upstreamRequest.hasAffine    = true;
    }

    // 上流へ伝播
    Node *upstream = upstreamNode(0);
    if (upstream) {
        return upstream->pullPrepare(upstreamRequest);  // パススルー
    }
    // 上流なし: 有効なデータがないのでサイズ0を返す
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    // width/height/originはデフォルト値（0）のまま
    return result;
}

// 複数のAffineNodeがある場合は行列を合成する
PrepareResponse AffineNode::onPushPrepare(const PrepareRequest &request)
{
    // 下流に渡すためのコピーを作成し、自身の行列を累積
    PrepareRequest downstreamRequest = request;
    if (downstreamRequest.hasPushAffine) {
        // Pull側と同じ合成順序にするため、自身の行列を先に掛ける
        // Pull側: M2 * M1（下流から上流へ伝播、後から掛ける）
        // Push側: M2 * M1（上流から下流へ伝播、先に掛ける）
        downstreamRequest.pushAffineMatrix = localMatrix_ * downstreamRequest.pushAffineMatrix;
    } else {
        downstreamRequest.pushAffineMatrix = localMatrix_;
        downstreamRequest.hasPushAffine    = true;
    }

    // 下流へ伝播
    Node *downstream = downstreamNode(0);
    if (downstream) {
        return downstream->pushPrepare(downstreamRequest);  // パススルー
    }
    // 下流なし: 有効なデータがないのでサイズ0を返す
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    // width/height/originはデフォルト値（0）のまま
    return result;
}

RenderResponse &AffineNode::onPullProcess(const RenderRequest &request)
{
    Node *upstream = upstreamNode(0);
    if (upstream) {
        return upstream->pullProcess(request);
    }
    return makeEmptyResponse(request.origin);
}

void AffineNode::onPushProcess(RenderResponse &input, const RenderRequest &request)
{
    Node *downstream = downstreamNode(0);
    if (downstream) {
        downstream->pushProcess(input, request);
    }
}

}  // namespace FLEXIMG_NAMESPACE
