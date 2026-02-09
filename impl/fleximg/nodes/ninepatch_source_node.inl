/**
 * @file ninepatch_source_node.inl
 * @brief NinePatchSourceNode 実装
 * @see src/fleximg/nodes/ninepatch_source_node.h
 */

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// NinePatchSourceNode - Template Method フック実装
// ============================================================================

PrepareResponse NinePatchSourceNode::onPullPrepare(const PrepareRequest &request)
{
    // ジオメトリ計算（まだなら）
    if (!geometryValid_) {
        updatePatchGeometry();
    }

    // AffineCapability: 自身のlocalMatrix_をrequest.affineMatrixと合成
    AffineMatrix combinedAffine;
    bool hasCombinedAffine = false;
    if (hasLocalTransform()) {
        if (request.hasAffine) {
            combinedAffine = request.affineMatrix * localMatrix_;
        } else {
            combinedAffine = localMatrix_;
        }
        hasCombinedAffine = true;
    } else if (request.hasAffine) {
        combinedAffine    = request.affineMatrix;
        hasCombinedAffine = true;
    }

    // 各区画のSourceNodeにPrepareRequestを伝播（スケール行列付き）
    for (int i = 0; i < 9; i++) {
        PrepareRequest patchRequest = request;
        patchRequest.hasAffine      = hasCombinedAffine;
        patchRequest.affineMatrix   = combinedAffine;

        // 親のアフィン行列と区画のスケール行列を合成
        if (patchNeedsAffine_[i]) {
            if (hasCombinedAffine) {
                // 親アフィン × 区画スケール
                patchRequest.affineMatrix = combinedAffine * patchScales_[i];
            } else {
                patchRequest.affineMatrix = patchScales_[i];
            }
            patchRequest.hasAffine = true;
        }
        // patchNeedsAffine_[i] == false の場合、親のアフィンをそのまま使用

        patches_[i].pullPrepare(patchRequest);
    }

    // NinePatchSourceNodeは終端なので上流への伝播なし
    // プルアフィン変換がある場合、出力側で必要なAABBを計算
    PrepareResponse result;
    result.status          = PrepareStatus::Prepared;
    result.preferredFormat = source_.formatID;

    if (hasCombinedAffine) {
        // positionを含めた行列を計算
        AffineMatrix matrixWithPos = combinedAffine;
        float transformedPosX      = matrixWithPos.a * positionX_ + matrixWithPos.b * positionY_;
        float transformedPosY      = matrixWithPos.c * positionX_ + matrixWithPos.d * positionY_;
        matrixWithPos.tx += transformedPosX;
        matrixWithPos.ty += transformedPosY;

        // 出力矩形に順変換を適用して出力側のAABBを計算
        calcAffineAABB(static_cast<float>(outputWidth_), static_cast<float>(outputHeight_), {pivotX_, pivotY_},
                       matrixWithPos, result.width, result.height, result.origin);
    } else {
        // アフィンなしの場合はそのまま（positionを含める）
        result.width  = static_cast<int16_t>(outputWidth_);
        result.height = static_cast<int16_t>(outputHeight_);
        // 新座標系: originはバッファ左上のワールド座標
        // position - origin = バッファ[0,0]のワールド座標
        result.origin.x = float_to_fixed(positionX_) - pivotX_;
        result.origin.y = float_to_fixed(positionY_) - pivotY_;
    }
    return result;
}

void NinePatchSourceNode::onPullFinalize()
{
    for (int i = 0; i < 9; i++) {
        patches_[i].pullFinalize();
    }
    finalize();
}

RenderResponse &NinePatchSourceNode::onPullProcess(const RenderRequest &request)
{
    if (!sourceValid_ || outputWidth_ <= 0 || outputHeight_ <= 0) {
        return makeEmptyResponse(request.origin);
    }

    // ジオメトリ計算（まだなら）
    if (!geometryValid_) {
        updatePatchGeometry();
    }

    // 描画順序: 伸縮パッチ → 固定パッチ
    // オーバーラップ領域で固定パッチが伸縮パッチの上に描画される
    // （バイリニア時のエッジを目立たなくするため）
    constexpr uint8_t drawOrder[9] = {
        4,           // 中央パッチ（両方向伸縮）を最初に
        1, 3, 5, 7,  // 伸縮パッチ（辺）
        0, 2, 6, 8   // 固定パッチ（角）を最後に
    };

    // キャンバス範囲を計算（全パッチのDataRangeを集計）
    int_fast16_t canvasStartX = INT16_MAX;
    int_fast16_t canvasEndX   = 0;
    for (auto i : drawOrder) {
        auto col = static_cast<int_fast16_t>(i % 3);
        auto row = static_cast<int_fast16_t>(i / 3);
        // ソースサイズが0のパッチはスキップ（伸縮パッチはマイナス出力でも描画）
        if (srcPatchW_[col] <= 0 || srcPatchH_[row] <= 0) {
            continue;
        }
        DataRange range = patches_[i].getDataRange(request);
        if (range.hasData()) {
            if (range.startX < canvasStartX) canvasStartX = range.startX;
            if (range.endX > canvasEndX) canvasEndX = range.endX;
        }
    }

    // 有効なデータがない場合は空を返す
    if (canvasStartX >= canvasEndX) {
        return makeEmptyResponse(request.origin);
    }

    int_fast16_t canvasWidth = canvasEndX - canvasStartX;

    // キャンバス作成（透明で初期化、必要幅のみ確保）
    int_fixed canvasOriginX = request.origin.x + to_fixed(canvasStartX);
    int_fixed canvasOriginY = request.origin.y;

    ImageBuffer canvasBuf = canvas_utils::createCanvas(canvasWidth, request.height, InitPolicy::Zero, allocator());
    ViewPort canvasView   = canvasBuf.view();

    // 全9区画を処理
    for (auto i : drawOrder) {
        // ソースサイズが0のパッチはスキップ（伸縮パッチはマイナス出力でも描画）
        auto col = static_cast<int_fast16_t>(i % 3);
        auto row = static_cast<int_fast16_t>(i / 3);
        if (srcPatchW_[col] <= 0 || srcPatchH_[row] <= 0) {
            continue;
        }

        // 範囲外のパッチはスキップ
        DataRange range = patches_[i].getDataRange(request);
        if (!range.hasData()) continue;

        RenderResponse &patchResult = patches_[i].pullProcess(request);
        if (!patchResult.isValid()) {
            context_->releaseResponse(patchResult);
            continue;
        }

        // フォーマット変換
        canvas_utils::ensureBlendableFormat(patchResult);

        // キャンバスに配置（全パッチ上書き）
        // NinePatchではパッチ同士の重なりは単純上書きで良い（EdgeFadeFlagsにより内部エッジはフェードアウトしない）
        canvas_utils::placeFirst(canvasView, canvasOriginX, canvasOriginY, patchResult.view(), patchResult.origin.x,
                                 patchResult.origin.y);

        // 使い終わったRenderResponseをプールに返却
        context_->releaseResponse(patchResult);
    }

    return makeResponse(std::move(canvasBuf), Point{canvasOriginX, canvasOriginY});
}

DataRange NinePatchSourceNode::getDataRange(const RenderRequest &request) const
{
    if (!sourceValid_ || outputWidth_ <= 0 || outputHeight_ <= 0) {
        return DataRange{0, 0};
    }

    // ジオメトリ計算（まだなら）
    if (!geometryValid_) {
        const_cast<NinePatchSourceNode *>(this)->updatePatchGeometry();
    }

    // 全パッチのデータ範囲の和集合を計算
    int_fast16_t startX = INT16_MAX;
    int_fast16_t endX   = INT16_MIN;

    for (int i = 0; i < 9; i++) {
        auto col = static_cast<int_fast16_t>(i % 3);
        auto row = static_cast<int_fast16_t>(i / 3);
        // ソースサイズが0のパッチはスキップ（伸縮パッチはマイナス出力でも描画）
        if (srcPatchW_[col] <= 0 || srcPatchH_[row] <= 0) {
            continue;
        }
        DataRange range = patches_[i].getDataRange(request);
        if (range.hasData()) {
            if (range.startX < startX) startX = range.startX;
            if (range.endX > endX) endX = range.endX;
        }
    }

    if (startX >= endX) {
        return DataRange{0, 0};
    }
    return DataRange{static_cast<int16_t>(startX), static_cast<int16_t>(endX)};
}

// ============================================================================
// NinePatchSourceNode - private ヘルパーメソッド実装
// ============================================================================

void NinePatchSourceNode::calcSrcPatchSizes()
{
    srcPatchW_[0] = srcLeft_;
    srcPatchW_[1] = source_.width - srcLeft_ - srcRight_;
    srcPatchW_[2] = srcRight_;
    srcPatchH_[0] = srcTop_;
    srcPatchH_[1] = source_.height - srcTop_ - srcBottom_;
    srcPatchH_[2] = srcBottom_;
}

void NinePatchSourceNode::calcAxisClipping(float outputSize, int_fast16_t srcFixed0, int_fast16_t srcFixed2,
                                           float &outWidth0, float &outWidth1, float &outWidth2, int16_t &effSrc0,
                                           int16_t &effSrc2)
{
    // ソースサイズは常に元のまま
    effSrc0 = static_cast<int16_t>(srcFixed0);
    effSrc2 = static_cast<int16_t>(srcFixed2);

    float totalFixed = static_cast<float>(srcFixed0 + srcFixed2);
    if (outputSize < totalFixed && totalFixed > 0) {
        // 全体幅が固定部合計より小さい場合：比率で按分（はみ出し防止）
        float ratio = outputSize / totalFixed;
        outWidth0   = static_cast<float>(srcFixed0) * ratio;
        outWidth2   = static_cast<float>(srcFixed2) * ratio;
        outWidth1   = 0.0f;  // 伸縮部は0（位置は左右固定の境界）
    } else {
        // 通常時：固定部はソースサイズ、伸縮部はマイナスも許容
        outWidth0 = static_cast<float>(srcFixed0);
        outWidth2 = static_cast<float>(srcFixed2);
        outWidth1 = outputSize - outWidth0 - outWidth2;
    }
}

void NinePatchSourceNode::updatePatchGeometry()
{
    if (!sourceValid_) return;

    // 横方向・縦方向のクリッピング計算
    calcAxisClipping(outputWidth_, srcLeft_, srcRight_, patchWidths_[0], patchWidths_[1], patchWidths_[2],
                     effectiveSrcLeft_, effectiveSrcRight_);
    calcAxisClipping(outputHeight_, srcTop_, srcBottom_, patchHeights_[0], patchHeights_[1], patchHeights_[2],
                     effectiveSrcTop_, effectiveSrcBottom_);

    // 各区画の出力開始位置
    patchOffsetX_[0] = 0.0f;
    patchOffsetX_[1] = patchWidths_[0];
    patchOffsetX_[2] = outputWidth_ - patchWidths_[2];
    patchOffsetY_[0] = 0.0f;
    patchOffsetY_[1] = patchHeights_[0];
    patchOffsetY_[2] = outputHeight_ - patchHeights_[2];

    // 各列/行の有効ソースサイズと開始位置
    int16_t effW[3] = {effectiveSrcLeft_, srcPatchW_[1], effectiveSrcRight_};
    int16_t effH[3] = {effectiveSrcTop_, srcPatchH_[1], effectiveSrcBottom_};
    int16_t srcX[3] = {0, srcLeft_, static_cast<int16_t>(source_.width - effectiveSrcRight_)};
    int16_t srcY[3] = {0, srcTop_, static_cast<int16_t>(source_.height - effectiveSrcBottom_)};

    // オーバーラップ有効判定（伸縮部の出力サイズが1以上の場合のみ）
    // 伸縮部が1未満になったらオーバーラップをオフにする
    bool hasHStretch = effW[1] > 0 && patchWidths_[1] >= 1.0f;
    bool hasVStretch = effH[1] > 0 && patchHeights_[1] >= 1.0f;

    float pivotXf = static_cast<float>(pivotX_) / INT_FIXED_ONE;
    float pivotYf = static_cast<float>(pivotY_) / INT_FIXED_ONE;

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;

            // オーバーラップ量（固定部→伸縮部方向に拡張）
            int_fast16_t dx = 0, dy = 0, dw = 0, dh = 0;

            // 横方向オーバーラップ（伸縮パッチがある場合、固定部を伸縮部側に拡張）
            if (hasHStretch) {
                if (col == 0 && effW[0] > 0) {
                    dw = 1;
                }  // 左固定: 右に拡張
                else if (col == 2 && effW[2] > 0) {
                    dx = -1;
                    dw = 1;
                }  // 右固定: 左に拡張
            }

            // 縦方向オーバーラップ（伸縮パッチがある場合、固定部を伸縮部側に拡張）
            if (hasVStretch) {
                if (row == 0 && effH[0] > 0) {
                    dh = 1;
                }  // 上固定: 下に拡張
                else if (row == 2 && effH[2] > 0) {
                    dy = -1;
                    dh = 1;
                }  // 下固定: 上に拡張
            }

            // ソースビュー設定
            if (effW[col] > 0 && effH[row] > 0) {
                ViewPort subView =
                    view_ops::subView(source_, srcX[col] + dx, srcY[row] + dy, effW[col] + dw, effH[row] + dh);
                patches_[idx].setSource(subView);
                patches_[idx].setPivot(0, 0);

                // エッジフェードアウト設定（外周の辺のみフェードアウト有効）
                // 隣接パッチとの境界（内部の辺）はフェードアウト無効
                uint8_t edgeFade = EdgeFade_None;
                if (row == 0) edgeFade |= EdgeFade_Top;     // 上端パッチ: 上辺フェード有効
                if (row == 2) edgeFade |= EdgeFade_Bottom;  // 下端パッチ: 下辺フェード有効
                if (col == 0) edgeFade |= EdgeFade_Left;    // 左端パッチ: 左辺フェード有効
                if (col == 2) edgeFade |= EdgeFade_Right;   // 右端パッチ: 右辺フェード有効
                patches_[idx].setEdgeFade(edgeFade);
            }

            // スケール計算（出力サイズ / ソースサイズ）
            float scaleX = 1.0f, scaleY = 1.0f;

            // 横方向スケール
            if (srcPatchW_[col] > 0) {
                scaleX = patchWidths_[col] / static_cast<float>(srcPatchW_[col]);
            }

            // 縦方向スケール
            if (srcPatchH_[row] > 0) {
                scaleY = patchHeights_[row] / static_cast<float>(srcPatchH_[row]);
            }

            // 平行移動量
            float tx = patchOffsetX_[col] + static_cast<float>(dx) - pivotXf + positionX_;
            float ty = patchOffsetY_[row] + static_cast<float>(dy) - pivotYf + positionY_;

            // バイリニア時の伸縮部位置補正
            // if (interpolationMode_ == InterpolationMode::Bilinear) {
            //     if (col == 1 && srcPatchW_[1] > 1) tx -= scaleX * 0.5f;
            //     if (row == 1 && srcPatchH_[1] > 1) ty -= scaleY * 0.5f;
            // }

            patchScales_[idx]      = AffineMatrix(scaleX, 0.0f, 0.0f, scaleY, tx, ty);
            patchNeedsAffine_[idx] = true;
        }
    }

    geometryValid_ = true;
}

}  // namespace FLEXIMG_NAMESPACE
