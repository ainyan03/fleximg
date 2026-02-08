/**
 * @file data_range_cache.h
 * @brief DataRange キャッシュヘルパー
 */

#ifndef FLEXIMG_DATA_RANGE_CACHE_H
#define FLEXIMG_DATA_RANGE_CACHE_H

#include "../image/data_range.h"
#include "../image/render_types.h"
#include "types.h"
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// DataRangeCache - RenderRequest キー専用キャッシュ
// ========================================================================
//
// getDataRange() の重複計算回避に使用。
// 同一スキャンライン（同一 origin + width）で複数回呼ばれるケースに対応。
//
// 使用例:
//   DataRangeCache cache_;
//
//   DataRange getDataRange(const RenderRequest& request) const override {
//     DataRange cached;
//     if (cache_.tryGet(request, cached)) {
//       return cached;
//     }
//     DataRange result = /* 計算 */;
//     cache_.set(request, result);
//     return result;
//   }
//
//   void onPrepare(...) override {
//     cache_.invalidate();  // 準備フェーズで無効化
//   }
//

class DataRangeCache {
public:
  DataRangeCache() = default;

  /// @brief キャッシュから取得を試みる
  /// @param request リクエスト（キー）
  /// @param out 取得結果の格納先
  /// @return true=キャッシュヒット, false=キャッシュミス
  bool tryGet(const RenderRequest &request, DataRange &out) const {
    if (!valid_) {
      return false;
    }
    if (cachedOrigin_.x != request.origin.x ||
        cachedOrigin_.y != request.origin.y || cachedWidth_ != request.width) {
      return false;
    }
    out = cachedRange_;
    return true;
  }

  /// @brief キャッシュに設定
  /// @param request リクエスト（キー）
  /// @param range 範囲（値）
  void set(const RenderRequest &request, const DataRange &range) {
    cachedOrigin_ = request.origin;
    cachedWidth_ = request.width;
    cachedRange_ = range;
    valid_ = true;
  }

  /// @brief キャッシュ無効化（Prepare時に呼び出し）
  void invalidate() { valid_ = false; }

  /// @brief キャッシュが有効か問い合わせ（テスト/デバッグ用）
  bool isValid() const { return valid_; }

private:
  Point cachedOrigin_ = {0, 0};
  int16_t cachedWidth_ = 0;
  DataRange cachedRange_ = {0, 0};
  bool valid_ = false;
};

} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_DATA_RANGE_CACHE_H
