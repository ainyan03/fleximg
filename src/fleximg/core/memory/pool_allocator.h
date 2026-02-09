/**
 * @file pool_allocator.h
 * @brief ビットマップベースのプールアロケータ
 *
 * 固定サイズブロックのプールを管理し、
 * フラグメンテーションを軽減します。
 */

#ifndef FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H
#define FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H

#include <cstddef>
#include <cstdint>

#include "../common.h"
#include "../perf_metrics.h"  // FLEXIMG_DEBUG_PERF_METRICS マクロ
#include "allocator.h"

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

// ========================================================================
// プールアロケータの統計情報（デバッグビルド時のみ有効）
// ========================================================================

#ifdef FLEXIMG_DEBUG_PERF_METRICS
struct PoolStats {
    size_t totalAllocations   = 0;  // 累計確保回数
    size_t totalDeallocations = 0;  // 累計解放回数
    size_t hits               = 0;  // 確保成功回数
    size_t misses             = 0;  // 確保失敗回数
    size_t peakUsedBlocks     = 0;  // 最大同時使用ブロック数
    uint32_t allocatedBitmap  = 0;  // 現在の使用状況（デバッグ用）

    void reset()
    {
        totalAllocations   = 0;
        totalDeallocations = 0;
        hits               = 0;
        misses             = 0;
        peakUsedBlocks     = 0;
        allocatedBitmap    = 0;
    }
};
#endif

// ========================================================================
// PoolAllocator - ビットマップベースのプールアロケータ
// ========================================================================
//
// 最大32ブロックまで対応（uint32_tビットマップ制限）
//

class PoolAllocator {
public:
    PoolAllocator() = default;
    ~PoolAllocator();

    // コピー禁止
    PoolAllocator(const PoolAllocator &)            = delete;
    PoolAllocator &operator=(const PoolAllocator &) = delete;

    /// @brief プールの初期化
    /// @param memory プール用メモリ領域（外部で確保済み）
    /// @param blockSize 各ブロックのサイズ
    /// @param blockCount ブロック数（最大32）
    /// @param isPSRAM プールがPSRAMかどうか
    /// @return 初期化成功ならtrue
    bool initialize(void *memory, size_t blockSize, size_t blockCount, bool isPSRAM = false);

    /// @brief メモリ確保（プールから）
    /// @param size 確保サイズ
    /// @return 確保したメモリへのポインタ（失敗時はnullptr）
    void *allocate(size_t size);

    /// @brief メモリ解放（プールへ）
    /// @param ptr 解放するメモリのポインタ
    /// @return プール内のポインタならtrue
    bool deallocate(void *ptr);

    /// @brief プールがPSRAMかどうか
    bool isPSRAM() const
    {
        return isPSRAM_;
    }

    /// @brief 初期化済みかどうか
    bool isInitialized() const
    {
        return initialized_;
    }

    /// @brief ブロックサイズ取得
    size_t blockSize() const
    {
        return blockSize_;
    }

    /// @brief ブロック数取得
    size_t blockCount() const
    {
        return blockCount_;
    }

    /// @brief 使用中ブロック数取得
    size_t usedBlockCount() const;

    /// @brief 空きブロック数取得
    size_t freeBlockCount() const
    {
        return blockCount_ - usedBlockCount();
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    /// @brief 統計情報取得（デバッグビルド時のみ）
    const PoolStats &stats() const
    {
        return stats_;
    }

    /// @brief 統計情報リセット（デバッグビルド時のみ）
    void resetStats()
    {
        stats_.reset();
    }

    /// @brief ピーク使用ブロック数のみリセット（デバッグビルド時のみ）
    void resetPeakStats()
    {
        stats_.peakUsedBlocks = 0;
    }
#endif

private:
    void *poolMemory_         = nullptr;  // プール用メモリ領域（外部管理）
    size_t blockSize_         = 0;        // ブロックサイズ
    size_t blockCount_        = 0;        // ブロック数
    bool isPSRAM_             = false;    // PSRAMかどうか
    uint32_t allocatedBitmap_ = 0;        // ブロック使用状況
    uint8_t blockCounts_[32]  = {};       // 各ブロックの確保ブロック数（連続確保対応）
    bool searchFromHead_      = true;     // 探索方向（交互に切り替え）
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PoolStats stats_;
#endif
    bool initialized_ = false;
};

// ========================================================================
// PoolAllocatorAdapter - IAllocatorインターフェースアダプタ
// ========================================================================
//
// PoolAllocatorをIAllocatorインターフェースでラップします。
// - プールから確保できない場合はDefaultAllocatorにフォールバック
// - FLEXIMG_DEBUG_PERF_METRICS定義時は統計情報を記録
//
// 使用例:
//   PoolAllocator pool;
//   pool.initialize(memory, 512, 32, false);
//   PoolAllocatorAdapter adapter(pool);
//   renderer.setAllocator(&adapter);
//

class PoolAllocatorAdapter : public IAllocator {
public:
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    /// @brief 統計情報（デバッグビルド時のみ有効）
    struct Stats {
        size_t poolHits        = 0;  ///< プールから確保成功
        size_t poolMisses      = 0;  ///< プールから確保失敗（フォールバック）
        size_t poolDeallocs    = 0;  ///< プールへ解放
        size_t defaultDeallocs = 0;  ///< DefaultAllocatorへ解放
        size_t lastAllocSize   = 0;  ///< 最後の確保サイズ

        void reset()
        {
            poolHits = poolMisses = poolDeallocs = defaultDeallocs = 0;
            lastAllocSize                                          = 0;
        }
    };
#endif

    /// @brief コンストラクタ
    /// @param pool 使用するPoolAllocator
    /// @param allowFallback
    /// プール確保失敗時にDefaultAllocatorへフォールバックするか
    explicit PoolAllocatorAdapter(PoolAllocator &pool, bool allowFallback = true)
        : pool_(pool), allowFallback_(allowFallback)
    {
    }

    void *allocate(size_t bytes, size_t /* alignment */ = 16) override
    {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        stats_.lastAllocSize = bytes;
#endif
        void *ptr = pool_.allocate(bytes);
        if (ptr) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            stats_.poolHits++;
#endif
            return ptr;
        }

        // プールから確保できない場合
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        stats_.poolMisses++;
#endif
        if (allowFallback_) {
            return DefaultAllocator::instance().allocate(bytes);
        }
        return nullptr;
    }

    void deallocate(void *ptr) override
    {
        if (pool_.deallocate(ptr)) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            stats_.poolDeallocs++;
#endif
        } else {
            // プール外のポインタはDefaultAllocatorで解放
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            stats_.defaultDeallocs++;
#endif
            if (allowFallback_) {
                DefaultAllocator::instance().deallocate(ptr);
            }
        }
    }

    const char *name() const override
    {
        return "PoolAllocatorAdapter";
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    /// @brief 統計情報取得（デバッグビルド時のみ）
    const Stats &stats() const
    {
        return stats_;
    }

    /// @brief 統計情報リセット（デバッグビルド時のみ）
    void resetStats()
    {
        stats_.reset();
    }
#endif

private:
    PoolAllocator &pool_;
    bool allowFallback_;
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    Stats stats_;
#endif
};

}  // namespace memory
}  // namespace core
}  // namespace FLEXIMG_NAMESPACE

#endif  // FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H
