/**
 * @file matte_node.inl
 * @brief MatteNode 実装
 * @see src/fleximg/nodes/matte_node.h
 */

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// MatteNode - Template Method フック実装
// ============================================================================

PrepareResponse MatteNode::onPullPrepare(const PrepareRequest &request)
{
    PrepareResponse merged;
    merged.status         = PrepareStatus::Prepared;
    bool hasValidUpstream = false;

    // AABB和集合計算用（ワールド座標）
    float minX = 0, minY = 0, maxX = 0, maxY = 0;

    // 全上流へ伝播し、結果をマージ（AABB和集合）
    for (int_fast16_t i = 0; i < 3; ++i) {
        Node *upstream = upstreamNode(i);
        if (upstream) {
            PrepareResponse result = upstream->pullPrepare(request);
            if (!result.ok()) {
                return result;  // エラーを伝播
            }

            // 新座標系: originはバッファ左上のワールド座標
            float left   = fixed_to_float(result.origin.x);
            float top    = fixed_to_float(result.origin.y);
            float right  = left + static_cast<float>(result.width);
            float bottom = top + static_cast<float>(result.height);

            if (!hasValidUpstream) {
                // 最初の結果でベースを初期化
                minX             = left;
                minY             = top;
                maxX             = right;
                maxY             = bottom;
                hasValidUpstream = true;
            } else {
                // 和集合（各辺のmin/max）
                if (left < minX) minX = left;
                if (top < minY) minY = top;
                if (right > maxX) maxX = right;
                if (bottom > maxY) maxY = bottom;
            }
        }
    }

    if (hasValidUpstream) {
        // 和集合結果をPrepareResponseに設定
        merged.width  = static_cast<int16_t>(std::ceil(maxX - minX));
        merged.height = static_cast<int16_t>(std::ceil(maxY - minY));
        // 新座標系: originはバッファ左上のワールド座標
        merged.origin.x = float_to_fixed(minX);
        merged.origin.y = float_to_fixed(minY);
        // MatteNodeは常にRGBA8_Straightで出力
        merged.preferredFormat = PixelFormatIDs::RGBA8_Straight;
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

    return merged;
}

void MatteNode::onPullFinalize()
{
    finalize();
    for (int_fast16_t i = 0; i < 3; ++i) {
        Node *upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullFinalize();
        }
    }
}

// ============================================================================
// MatteNode - getDataRange実装
// ============================================================================

DataRange MatteNode::calcUpstreamRanges(const RenderRequest &request) const
{
    Node *fgNode   = upstreamNode(0);
    Node *bgNode   = upstreamNode(1);
    Node *maskNode = upstreamNode(2);

    // 各上流のデータ範囲を取得
    rangeCache_.fgRange   = fgNode ? fgNode->getDataRange(request) : DataRange{};
    rangeCache_.bgRange   = bgNode ? bgNode->getDataRange(request) : DataRange{};
    rangeCache_.maskRange = maskNode ? maskNode->getDataRange(request) : DataRange{};

    // 有効範囲を計算: bg ∪ (mask ∩ fg)
    // - bgは常に有効（マスク範囲外やalpha=0でbgが見える）
    // - fgはマスク範囲との交差部分のみ有効
    // - マスクのみ（fg/bgなし）は透明なので除外
    int16_t startX = request.width;
    int16_t endX   = 0;

    // bg範囲は常に有効
    if (rangeCache_.bgRange.hasData()) {
        startX = rangeCache_.bgRange.startX;
        endX   = rangeCache_.bgRange.endX;
    }

    // (mask ∩ fg)範囲を追加
    if (rangeCache_.maskRange.hasData() && rangeCache_.fgRange.hasData()) {
        int16_t intersectStart = std::max(rangeCache_.maskRange.startX, rangeCache_.fgRange.startX);
        int16_t intersectEnd   = std::min(rangeCache_.maskRange.endX, rangeCache_.fgRange.endX);
        if (intersectStart < intersectEnd) {
            if (intersectStart < startX) startX = intersectStart;
            if (intersectEnd > endX) endX = intersectEnd;
        }
    }

    rangeCache_.unionRange = (startX < endX) ? DataRange{startX, endX} : DataRange{};
    rangeCache_.origin     = request.origin;
    rangeCache_.valid      = true;

    return rangeCache_.unionRange;
}

DataRange MatteNode::getDataRange(const RenderRequest &request) const
{
    // キャッシュが有効でoriginが一致すれば再利用
    if (rangeCache_.valid && rangeCache_.origin.x == request.origin.x && rangeCache_.origin.y == request.origin.y) {
        return rangeCache_.unionRange;
    }
    return calcUpstreamRanges(request);
}

// ============================================================================
// MatteNode - onPullProcess実装（最適化版）
// ============================================================================
//
// 処理フロー:
// 1. mask有効範囲の確定（早期リターン）
//    - maskデータなし / 取得失敗 / 全面0 → bg直接返却（変換なし）
// 2. bg取得・出力領域計算
// 3. bg戦略決定・出力バッファ作成
// 4. fg取得（mask有効範囲のみ）
// 5. 合成
//

RenderResponse &MatteNode::onPullProcess(const RenderRequest &request)
{
    Node *fgNode   = upstreamNode(0);  // 前景
    Node *bgNode   = upstreamNode(1);  // 背景
    Node *maskNode = upstreamNode(2);  // マスク

    // ========================================================================
    // Step 1: mask取得・全面0判定
    // ========================================================================

    // キャッシュ確認・更新
    if (!rangeCache_.valid || rangeCache_.origin.x != request.origin.x || rangeCache_.origin.y != request.origin.y) {
        calcUpstreamRanges(request);
    }

    {
        // maskデータなし or maskNodeなし → bg fallback
        if (!rangeCache_.maskRange.hasData()) goto fallback_bg;
        if (!maskNode) goto fallback_bg;

        // mask要求範囲をfg∪bgの有効X範囲に制限
        // fg/bgが存在しない領域のマスクは取得しても無駄
        int16_t fgBgStart = request.width;
        int16_t fgBgEnd   = 0;
        if (rangeCache_.fgRange.hasData()) {
            if (rangeCache_.fgRange.startX < fgBgStart) fgBgStart = rangeCache_.fgRange.startX;
            if (rangeCache_.fgRange.endX > fgBgEnd) fgBgEnd = rangeCache_.fgRange.endX;
        }
        if (rangeCache_.bgRange.hasData()) {
            if (rangeCache_.bgRange.startX < fgBgStart) fgBgStart = rangeCache_.bgRange.startX;
            if (rangeCache_.bgRange.endX > fgBgEnd) fgBgEnd = rangeCache_.bgRange.endX;
        }

        // fg∪bgが空 → マスク値に関わらず出力は透明
        if (fgBgStart >= fgBgEnd) {
            rangeCache_.valid = false;
            return makeEmptyResponse(request.origin);
        }

        // fg∪bgとmaskの交差範囲でmask要求を絞る
        RenderRequest maskRequest = request;
        {
            int16_t clampStart = std::max(fgBgStart, rangeCache_.maskRange.startX);
            int16_t clampEnd   = std::min(fgBgEnd, rangeCache_.maskRange.endX);
            if (clampStart < clampEnd) {
                maskRequest.origin.x = request.origin.x + to_fixed(clampStart);
                maskRequest.width    = clampEnd - clampStart;
            }
        }

        RenderResponse &maskResult = maskNode->pullProcess(maskRequest);
        if (!maskResult.isValid()) goto fallback_bg;

        // バッファ準備
        consolidateIfNeeded(maskResult);

        // Alpha8に変換
        if (maskResult.buffer().formatID() != PixelFormatIDs::Alpha8) {
            maskResult.convertFormat(PixelFormatIDs::Alpha8);
        }

        // 全面0判定（行スキャン）+ 有効範囲へのcrop
        ViewPort maskView         = maskResult.view();
        const uint8_t *maskData   = static_cast<const uint8_t *>(maskView.data);
        int_fast16_t maskLeftSkip = 0, maskRightSkip = 0;
        auto maskEffectiveWidth = scanMaskZeroRanges(maskData, maskView.width, maskLeftSkip, maskRightSkip);

        // 全面0 → bg fallback
        if (maskEffectiveWidth == 0) goto fallback_bg;

        // マスクを有効範囲にcrop（左右の0領域をスキップ）
        if (maskLeftSkip > 0 || maskRightSkip > 0) {
            maskResult.buffer().cropView(static_cast<int_fast16_t>(maskLeftSkip), 0,
                                         static_cast<int_fast16_t>(maskEffectiveWidth),
                                         static_cast<int_fast16_t>(maskView.height));
            maskResult.origin.x += to_fixed(maskLeftSkip);
            maskView = maskResult.view();  // cropされたビューを再取得
        }

        // ========================================================================
        // Step 2: bg取得・出力領域計算
        // ========================================================================

        RenderResponse *bgResultPtr = nullptr;
        if (rangeCache_.bgRange.hasData() && bgNode) {
            RenderResponse &bgResult = bgNode->pullProcess(request);
            if (bgResult.isValid()) {
                // バッファ準備
                consolidateIfNeeded(bgResult);
                bgResultPtr = &bgResult;
            }
        }

        // 出力領域計算（cropされたmask ∪ bg）
        int_fixed unionMinX = maskResult.origin.x;
        int_fixed unionMinY = maskResult.origin.y;
        int_fixed unionMaxX = unionMinX + to_fixed(maskView.width);
        int_fixed unionMaxY = unionMinY + to_fixed(maskView.height);

        if (bgResultPtr) {
            ViewPort bgViewPort = bgResultPtr->view();
            int_fixed bgMinX    = bgResultPtr->origin.x;
            int_fixed bgMinY    = bgResultPtr->origin.y;
            int_fixed bgMaxX    = bgMinX + to_fixed(bgViewPort.width);
            int_fixed bgMaxY    = bgMinY + to_fixed(bgViewPort.height);
            if (bgMinX < unionMinX) unionMinX = bgMinX;
            if (bgMinY < unionMinY) unionMinY = bgMinY;
            if (bgMaxX > unionMaxX) unionMaxX = bgMaxX;
            if (bgMaxY > unionMaxY) unionMaxY = bgMaxY;
        }

        auto unionWidth  = static_cast<int_fast16_t>(from_fixed(unionMaxX - unionMinX));
        auto unionHeight = static_cast<int_fast16_t>(from_fixed(unionMaxY - unionMinY));

        // ========================================================================
        // Step 3: 出力バッファ作成（ゼロクリア）+ bgコピー
        // ========================================================================

        FLEXIMG_METRICS_SCOPE(NodeType::Matte);

        ImageBuffer outputBuf(unionWidth, unionHeight, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, allocator());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Matte].recordAlloc(outputBuf.totalBytes(), outputBuf.width(),
                                                                   outputBuf.height());
#endif

        // bgがあればコピー
        if (bgResultPtr) {
            auto bgOffsetX = static_cast<int_fast16_t>(from_fixed(bgResultPtr->origin.x - unionMinX));
            auto bgOffsetY = static_cast<int_fast16_t>(from_fixed(bgResultPtr->origin.y - unionMinY));

            auto converter = resolveConverter(bgResultPtr->buffer().formatID(), PixelFormatIDs::RGBA8_Straight,
                                              &bgResultPtr->buffer().auxInfo());
            if (converter) {
                ViewPort bgViewPort   = bgResultPtr->view();
                ViewPort outView      = outputBuf.view();
                auto srcBytesPerPixel = static_cast<int_fast16_t>(bgViewPort.bytesPerPixel());

                // bgの有効範囲を計算（出力座標系）
                auto copyStartX = std::max<int_fast16_t>(0, bgOffsetX);
                auto copyEndX   = std::min<int_fast16_t>(unionWidth, bgOffsetX + bgViewPort.width);
                auto copyStartY = std::max<int_fast16_t>(0, bgOffsetY);
                auto copyEndY   = std::min<int_fast16_t>(unionHeight, bgOffsetY + bgViewPort.height);
                auto copyWidth  = static_cast<int_fast16_t>(copyEndX - copyStartX);

                if (copyWidth > 0) {
                    auto srcStartX = static_cast<int_fast16_t>(copyStartX - bgOffsetX);
                    for (auto y = copyStartY; y < copyEndY; ++y) {
                        auto srcY             = static_cast<int_fast16_t>(y - bgOffsetY);
                        const uint8_t *srcRow = static_cast<const uint8_t *>(bgViewPort.data) +
                                                (bgViewPort.y + srcY) * bgViewPort.stride +
                                                (bgViewPort.x + srcStartX) * srcBytesPerPixel;
                        uint8_t *dstRow = static_cast<uint8_t *>(outView.data) + y * outView.stride + copyStartX * 4;
                        converter(dstRow, srcRow, static_cast<size_t>(copyWidth));
                    }
                }
            }
        }

        // ========================================================================
        // Step 4: fg取得
        // ========================================================================

        RenderResponse *fgResultPtr = nullptr;
        if (fgNode && rangeCache_.fgRange.hasData()) {
            RenderResponse &fgResult = fgNode->pullProcess(request);
            if (fgResult.isValid()) {
                // バッファ準備
                consolidateIfNeeded(fgResult);
                // RGBA8_Straightに変換
                if (fgResult.buffer().formatID() != PixelFormatIDs::RGBA8_Straight) {
                    fgResult.convertFormat(PixelFormatIDs::RGBA8_Straight);
                }
                fgResultPtr = &fgResult;
            }
        }

        // ========================================================================
        // Step 5: 合成
        // ========================================================================

        // InputView::fromにはconst参照が必要なので、一時的なRenderResponseを使う
        static RenderResponse emptyResult;
        emptyResult.origin = Point{};

        InputView fgView        = InputView::from(fgResultPtr ? *fgResultPtr : emptyResult, unionMinX, unionMinY);
        InputView maskInputView = InputView::from(maskResult, unionMinX, unionMinY);

        applyMatteOverlay(outputBuf, unionWidth, fgView, maskInputView);

        // キャッシュ無効化
        rangeCache_.valid = false;

        return makeResponse(std::move(outputBuf), Point{unionMinX, unionMinY});
    }

    // bgフォールバック: mask無効時はbgを直接返却
fallback_bg:
    rangeCache_.valid = false;
    if (bgNode) {
        return bgNode->pullProcess(request);
    }
    return makeEmptyResponse(request.origin);
}

// ============================================================================
// MatteNode - ヘルパー関数実装
// ============================================================================

int_fast16_t MatteNode::scanMaskZeroRanges(const uint8_t *maskData, int_fast16_t maskWidth, int_fast16_t &outLeftSkip,
                                           int_fast16_t &outRightSkip)
{
    // 左端からの0スキップ（4バイト単位、アライメント対応）
    int_fast16_t leftSkip = 0;
    {
        // Phase 1: アライメントまで1バイトずつ
        uintptr_t addr        = reinterpret_cast<uintptr_t>(maskData);
        int_fast16_t misalign = static_cast<int_fast16_t>(addr & 3);
        if (misalign != 0) {
            int_fast16_t alignBytes = static_cast<int_fast16_t>(4 - misalign);
            if (alignBytes > maskWidth) {
                alignBytes = maskWidth;
            }
            while (leftSkip < alignBytes && maskData[leftSkip] == 0) {
                ++leftSkip;
            }
            alignBytes -= leftSkip;
            if (leftSkip < maskWidth && maskData[leftSkip] != 0) {
                outLeftSkip  = leftSkip;
                outRightSkip = 0;
                // 右スキップも計算して返す
                goto scan_right;
            }
        }

        // Phase 2: 4バイト単位（ポインタベース）
        {
            const uint32_t *p32     = reinterpret_cast<const uint32_t *>(maskData + leftSkip);
            const uint32_t *p32_end = p32 + ((maskWidth - leftSkip) >> 2);
            while (p32 < p32_end && *p32 == 0) {
                ++p32;
            }
            leftSkip = static_cast<int_fast16_t>(reinterpret_cast<const uint8_t *>(p32) - maskData);
        }

        // Phase 3: 残りを1バイトずつ
        while (leftSkip < maskWidth && maskData[leftSkip] == 0) {
            ++leftSkip;
        }
    }

    // 全面0なら終了
    if (leftSkip >= maskWidth) {
        outLeftSkip  = maskWidth;
        outRightSkip = 0;
        return 0;
    }

scan_right:
    outLeftSkip = leftSkip;

    // 右端からの0スキップ（4バイト単位、アライメント対応）
    int_fast16_t rightSkip = 0;
    {
        const int_fast16_t limit = static_cast<int_fast16_t>(maskWidth - leftSkip);

        // Phase 1: アライメントまで1バイトずつ
        uintptr_t endAddr     = reinterpret_cast<uintptr_t>(maskData + maskWidth);
        int_fast16_t misalign = static_cast<int_fast16_t>(endAddr & 3);
        if (misalign > limit) {
            misalign = limit;
        }
        while (rightSkip < misalign && maskData[maskWidth - 1 - rightSkip] == 0) {
            ++rightSkip;
        }
        if (rightSkip < limit && maskData[maskWidth - 1 - rightSkip] != 0) {
            outRightSkip = rightSkip;
            return maskWidth - leftSkip - rightSkip;
        }

        // Phase 2: 4バイト単位（ポインタベース）
        {
            const uint32_t *p32     = reinterpret_cast<const uint32_t *>(maskData + maskWidth - rightSkip) - 1;
            const uint32_t *p32_end = p32 - ((limit - rightSkip) >> 2);
            while (p32 > p32_end && *p32 == 0) {
                --p32;
            }
            rightSkip = static_cast<int_fast16_t>(maskData + maskWidth - reinterpret_cast<const uint8_t *>(p32 + 1));
        }

        // Phase 3: 残りを1バイトずつ
        while (rightSkip < limit && maskData[maskWidth - 1 - rightSkip] == 0) {
            ++rightSkip;
        }
    }

    outRightSkip = rightSkip;
    return maskWidth - leftSkip - rightSkip;
}

// ============================================================================
// MatteNode - 合成処理実装
// ============================================================================

// ----------------------------------------------------------------------------
// processRowNoFg: fgなし領域の行処理
// - alpha=0: スキップ（出力には既にbgがある）
// - alpha=255: 透明(0,0,0,0)に書き込み（fgがないため）
// - 中間alpha: bgをフェード（out = out * (1-alpha)）
// ----------------------------------------------------------------------------
static inline void processRowNoFg(uint8_t *__restrict__ d, const uint8_t *__restrict__ m, int_fast16_t pixelCount)
{
    if (pixelCount <= 0) return;

    uint_fast8_t alpha = *m;

    if (alpha == 0) goto handle_alpha_0;
    if (alpha == 255) goto handle_alpha_255;

blend:
    // ブレンドループ: do-whileで末尾デクリメント
    // breakした時点でpixelCountはまだ減っていない
    --m;
    d -= 4;
    do {
        ++m;
        alpha = *m;
        d += 4;
        if (alpha == 0) break;
        if (alpha == 255) break;
        uint32_t d32 = *reinterpret_cast<uint32_t *>(d);
        // bgフェードのみ（fgなし）: out = bg * (1-alpha)
        // 256スケール正規化: inv_a_256 = 256 - (alpha + (alpha >> 7))
        // 精度: 91.6%が完全一致、最大誤差±1
        uint_fast16_t inv_a_256 = 256 - alpha - (alpha >> 7);
        uint32_t d32_even       = d32 & 0x00FF00FF;
        uint32_t d32_odd        = (d32 >> 8) & 0x00FF00FF;
        d32_even *= inv_a_256;
        d32_odd *= inv_a_256;
        *reinterpret_cast<uint32_t *>(d) = d32_odd;
        d[0]                             = static_cast<uint8_t>(d32_even >> 8);
        d[2]                             = static_cast<uint8_t>(d32_even >> 24);
    } while (--pixelCount > 0);
    if (pixelCount <= 0) return;
    if (alpha == 0) goto handle_alpha_0;

handle_alpha_255:
    // alpha=255でfgなし → 透明(0,0,0,0)に書き込み
    *reinterpret_cast<uint32_t *>(d) = 0;
    if (--pixelCount <= 0) return;
    ++m;
    alpha = *m;
    d += 4;
    // 4px単位で透明書き込み
    {
        auto plimit = pixelCount >> 2;
        if (plimit && alpha == 255 && (reinterpret_cast<uintptr_t>(m) & 3) == 0) {
            uint32_t m32 = reinterpret_cast<const uint32_t *>(m)[0];
            auto m_start = m;
            do {
                if (m32 != 0xFFFFFFFFu) break;
                m32 = reinterpret_cast<const uint32_t *>(m)[1];
                m += 4;
            } while (--plimit);
            if (m != m_start) {
                auto len = static_cast<int_fast16_t>(m - m_start);
                std::memset(d, 0, static_cast<size_t>(len) * 4);
                pixelCount -= len;
                if (pixelCount <= 0) return;
                alpha = static_cast<uint_fast8_t>(m32);
                d += len * 4;
            }
        }
    }
    if (alpha == 255) goto handle_alpha_255;
    if (alpha != 0) goto blend;

handle_alpha_0:
    if (--pixelCount <= 0) return;
    ++m;
    alpha = *m;
    d += 4;
    // 4px単位スキップ
    {
        auto plimit = pixelCount >> 2;
        if (plimit && alpha == 0 && (reinterpret_cast<uintptr_t>(m) & 3) == 0) {
            uint32_t m32 = reinterpret_cast<const uint32_t *>(m)[0];
            auto m_start = m;
            do {
                if (m32 != 0) break;
                m32 = reinterpret_cast<const uint32_t *>(m)[1];
                m += 4;
            } while (--plimit);
            if (m != m_start) {
                auto skipped = static_cast<int_fast16_t>(m - m_start);
                pixelCount -= skipped;
                if (pixelCount <= 0) return;
                alpha = static_cast<uint_fast8_t>(m32);
                d += skipped * 4;
            }
        }
    }
    if (alpha == 0) goto handle_alpha_0;
    if (alpha == 255) goto handle_alpha_255;
    goto blend;
}

// ----------------------------------------------------------------------------
// processRowWithFg: fg領域の行処理
// - alpha=0: スキップ（出力には既にbgがある）
// - alpha=255: fgをコピー
// - 中間alpha: フルブレンド（out = out*(1-alpha) + fg*alpha）
// ----------------------------------------------------------------------------
static inline void processRowWithFg(uint8_t *__restrict__ d, const uint8_t *__restrict__ m,
                                    const uint8_t *__restrict__ s, int_fast16_t pixelCount)
{
    if (pixelCount <= 0) return;

    uint_fast8_t alpha = *m;

    if (alpha == 0) goto handle_alpha_0;
    if (alpha == 255) goto handle_alpha_255;

blend:
    // ブレンドループ: do-whileで末尾デクリメント
    // breakした時点でpixelCountはまだ減っていない
    --m;
    d -= 4;
    s -= 4;
    do {
        ++m;
        alpha = *m;
        d += 4;
        s += 4;
        if (alpha == 0) break;
        if (alpha == 255) break;
        uint32_t d32 = *reinterpret_cast<uint32_t *>(d);
        uint32_t s32 = *reinterpret_cast<const uint32_t *>(s);
        // fg/bg両方のブレンド: out = bg*(1-alpha) + fg*alpha
        // 256スケール正規化: alpha_256 = alpha + (alpha >> 7)
        // 精度: 91.6%が完全一致、最大誤差±1
        uint_fast16_t alpha_256          = alpha + (alpha >> 7);
        uint_fast16_t inv_a_256          = 256 - alpha_256;
        uint32_t d32_even                = d32 & 0x00FF00FF;
        uint32_t s32_even                = s32 & 0x00FF00FF;
        uint32_t d32_odd                 = (d32 >> 8) & 0x00FF00FF;
        uint32_t s32_odd                 = (s32 >> 8) & 0x00FF00FF;
        d32_odd                          = d32_odd * inv_a_256 + s32_odd * alpha_256;
        d32_even                         = d32_even * inv_a_256 + s32_even * alpha_256;
        *reinterpret_cast<uint32_t *>(d) = d32_odd;
        d[0]                             = static_cast<uint8_t>(d32_even >> 8);
        d[2]                             = static_cast<uint8_t>(d32_even >> 24);
    } while (--pixelCount > 0);
    if (pixelCount <= 0) return;
    if (alpha == 0) goto handle_alpha_0;

handle_alpha_255:
    // fgをコピー
    *reinterpret_cast<uint32_t *>(d) = *reinterpret_cast<const uint32_t *>(s);
    if (--pixelCount <= 0) return;
    ++m;
    alpha = *m;
    d += 4;
    s += 4;
    // 4px単位コピー
    {
        auto plimit = pixelCount >> 2;
        if (plimit && alpha == 255 && (reinterpret_cast<uintptr_t>(m) & 3) == 0) {
            uint32_t m32 = reinterpret_cast<const uint32_t *>(m)[0];
            auto m_start = m;
            do {
                if (m32 != 0xFFFFFFFFu) break;
                m32 = reinterpret_cast<const uint32_t *>(m)[1];
                m += 4;
            } while (--plimit);
            if (m != m_start) {
                auto len = static_cast<int_fast16_t>(m - m_start);
                memcpy(d, s, static_cast<size_t>(len) * 4);
                pixelCount -= len;
                if (pixelCount <= 0) return;
                alpha = static_cast<uint_fast8_t>(m32);
                d += len * 4;
                s += len * 4;
            }
        }
    }
    if (alpha == 255) goto handle_alpha_255;
    if (alpha != 0) goto blend;

handle_alpha_0:
    if (--pixelCount <= 0) return;
    ++m;
    alpha = *m;
    d += 4;
    s += 4;
    // 4px単位スキップ
    {
        auto plimit = pixelCount >> 2;
        if (plimit && alpha == 0 && (reinterpret_cast<uintptr_t>(m) & 3) == 0) {
            uint32_t m32 = reinterpret_cast<const uint32_t *>(m)[0];
            auto m_start = m;
            do {
                if (m32 != 0) break;
                m32 = reinterpret_cast<const uint32_t *>(m)[1];
                m += 4;
            } while (--plimit);
            if (m != m_start) {
                auto skipped = static_cast<int_fast16_t>(m - m_start);
                pixelCount -= skipped;
                if (pixelCount <= 0) return;
                alpha = static_cast<uint_fast8_t>(m32);
                d += skipped * 4;
                s += skipped * 4;
            }
        }
    }
    if (alpha == 0) goto handle_alpha_0;
    if (alpha == 255) goto handle_alpha_255;
    goto blend;
}

// ----------------------------------------------------------------------------

void MatteNode::applyMatteOverlay(ImageBuffer &output, int_fast16_t outWidth, const InputView &fg,
                                  const InputView &mask)
{
    ViewPort outView              = output.view();
    uint8_t *__restrict__ outData = static_cast<uint8_t *>(outView.data);
    const auto outHeight          = static_cast<int_fast16_t>(outView.height);
    const int32_t outStride       = outView.stride;

    // マスクの有効X範囲（出力座標系）
    const auto maskXStart = std::max<int_fast16_t>(0, mask.offsetX);
    const auto maskXEnd   = std::min<int_fast16_t>(outWidth, mask.width + mask.offsetX);
    if (maskXStart >= maskXEnd) return;

    const auto maskSrcOffsetX = static_cast<int_fast16_t>(maskXStart - mask.offsetX);

    // 前景の有効X範囲（事前計算）
    const auto fgXStart     = fg.valid() ? std::max<int_fast16_t>(maskXStart, fg.offsetX) : maskXEnd;
    const auto fgXEnd       = fg.valid() ? std::min<int_fast16_t>(maskXEnd, fg.width + fg.offsetX) : maskXStart;
    const auto fgSrcOffsetX = static_cast<int_fast16_t>(fgXStart - fg.offsetX);

    // 3領域の幅を事前計算
    const auto leftWidth  = static_cast<int_fast16_t>(fgXStart - maskXStart);  // 左領域（fgなし）
    const auto midWidth   = static_cast<int_fast16_t>(fgXEnd - fgXStart);      // 中央領域（fg/bg両方）
    const auto rightWidth = static_cast<int_fast16_t>(maskXEnd - fgXEnd);      // 右領域（fgなし）

    // 行ごとに処理
    for (int_fast16_t y = 0; y < outHeight; ++y) {
        // マスクがない行 → スキップ
        const uint8_t *maskRowBase = mask.rowAt(y);
        if (!maskRowBase) continue;

        // ベースポインタ
        const uint8_t *__restrict__ mBase = maskRowBase + maskSrcOffsetX;
        uint8_t *__restrict__ dBase       = outData + y * outStride + maskXStart * 4;

        // 左領域: fgなし
        if (leftWidth > 0) {
            processRowNoFg(dBase, mBase, static_cast<int_fast16_t>(leftWidth));
        }

        // 中央領域: fg/bg両方
        if (midWidth > 0) {
            const uint8_t *fgRowBase = fg.rowAt(y);
            if (fgRowBase) {
                processRowWithFg(dBase + leftWidth * 4, mBase + leftWidth, fgRowBase + fgSrcOffsetX * 4,
                                 static_cast<int_fast16_t>(midWidth));
            }
        }

        // 右領域: fgなし
        if (rightWidth > 0) {
            processRowNoFg(dBase + (leftWidth + midWidth) * 4, mBase + leftWidth + midWidth,
                           static_cast<int_fast16_t>(rightWidth));
        }
    }
}

#if defined(BENCH_M5STACK) || defined(BENCH_NATIVE)
// ============================================================================
// MatteNode - ベンチマーク用ラッパー関数
// ============================================================================

void MatteNode::benchProcessRowWithFg(uint8_t *d, const uint8_t *m, const uint8_t *s, int pixelCount)
{
    processRowWithFg(d, m, s, static_cast<int_fast16_t>(pixelCount));
}

void MatteNode::benchProcessRowNoFg(uint8_t *d, const uint8_t *m, int pixelCount)
{
    processRowNoFg(d, m, static_cast<int_fast16_t>(pixelCount));
}
#endif

}  // namespace FLEXIMG_NAMESPACE
