/**
 * @file common.h
 * @brief Common definitions for fleximg library
 */

#ifndef FLEXIMG_COMMON_H
#define FLEXIMG_COMMON_H

// Namespace definition (types.h より前に必要)
#ifndef FLEXIMG_NAMESPACE
#define FLEXIMG_NAMESPACE fleximg
#endif

#include "types.h"

#include <cstdio>   // printf
#include <cstdlib>  // std::abort

// ========================================================================
// デバッグログマクロ
// ========================================================================
//
// FLEXIMG_DEBUG_LOG(fmt, ...): デバッグメッセージ出力 + flush
//   - ARDUINO環境: printf + fflush + vTaskDelay(1)
//   - その他: printf + fflush
//
// FLEXIMG_DEBUG_WARN(fmt, ...): 警告出力（デバッグビルドのみ有効）
//
// リリースビルドでは FLEXIMG_DEBUG_WARN は無効化される。
// FLEXIMG_DEBUG_LOG は ASSERT/REQUIRE から使用するため常に定義。
//

#ifdef ARDUINO
#define FLEXIMG_DEBUG_LOG(fmt, ...)    \
    do {                               \
        printf(fmt "\n", __VA_ARGS__); \
        fflush(stdout);                \
        vTaskDelay(1);                 \
    } while (0)
#else
#define FLEXIMG_DEBUG_LOG(fmt, ...)    \
    do {                               \
        printf(fmt "\n", __VA_ARGS__); \
        fflush(stdout);                \
    } while (0)
#endif

#ifdef FLEXIMG_DEBUG
#define FLEXIMG_DEBUG_WARN(fmt, ...) FLEXIMG_DEBUG_LOG(fmt, __VA_ARGS__)
#else
#define FLEXIMG_DEBUG_WARN(fmt, ...) ((void)0)
#endif

// ========================================================================
// Debug/Release assertion macros
// ========================================================================
//
// FLEXIMG_ASSERT(cond, msg): デバッグ時のみ有効（リリースでは無効化）
// FLEXIMG_REQUIRE(cond, msg): 常に有効（致命的エラー検出用）
//

#ifdef FLEXIMG_DEBUG
#define FLEXIMG_ASSERT(cond, msg)                      \
    do {                                               \
        if (!(cond)) {                                 \
            FLEXIMG_DEBUG_LOG("ASSERT FAIL: %s", msg); \
            std::abort();                              \
        }                                              \
    } while (0)
#else
#define FLEXIMG_ASSERT(cond, msg) ((void)0)
#endif

#define FLEXIMG_REQUIRE(cond, msg)                      \
    do {                                                \
        if (!(cond)) {                                  \
            FLEXIMG_DEBUG_LOG("REQUIRE FAIL: %s", msg); \
            std::abort();                               \
        }                                               \
    } while (0)

// ========================================================================
// Deprecated attribute
// ========================================================================

#if __cplusplus >= 201402L
#define FLEXIMG_DEPRECATED(msg) [[deprecated(msg)]]
#else
#define FLEXIMG_DEPRECATED(msg)
#endif

// Version information
#define FLEXIMG_VERSION_MAJOR 2
#define FLEXIMG_VERSION_MINOR 0
#define FLEXIMG_VERSION_PATCH 0

#endif  // FLEXIMG_COMMON_H
