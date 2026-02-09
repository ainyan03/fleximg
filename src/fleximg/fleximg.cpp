/**
 * @file fleximg.cpp
 * @brief fleximg ライブラリ実装のコンパイル単位
 *
 * このファイルは fleximg ライブラリの唯一のコンパイル単位です。
 * FLEXIMG_IMPLEMENTATION を定義してから各ヘッダをincludeすることで、
 * 実装部がここでのみコンパイルされます。
 *
 * stb ライブラリ方式（Implementation Macro パターン）を採用しています。
 */

#define FLEXIMG_IMPLEMENTATION

// =============================================================================
// 宣言ヘッダ
// =============================================================================

// Core
#include "core/affine_capability.h"
#include "core/memory/platform.h"
#include "core/memory/pool_allocator.h"
#include "core/node.h"

// Image
#include "image/pixel_format.h"
#include "image/viewport.h"

// Operations
#include "operations/filters.h"

// Nodes
#include "nodes/affine_node.h"
#include "nodes/composite_node.h"
#include "nodes/distributor_node.h"
#include "nodes/filter_node_base.h"
#include "nodes/horizontal_blur_node.h"
#include "nodes/matte_node.h"
#include "nodes/ninepatch_source_node.h"
#include "nodes/renderer_node.h"
#include "nodes/sink_node.h"
#include "nodes/source_node.h"
#include "nodes/vertical_blur_node.h"

// =============================================================================
// 実装 (impl/)
// =============================================================================

// Core
#include "../../impl/fleximg/core/memory/platform.inl"
#include "../../impl/fleximg/core/memory/pool_allocator.inl"
#include "../../impl/fleximg/core/node.inl"

// Image (pixel_format)
#include "../../impl/fleximg/image/pixel_format.inl"

// Image
#include "../../impl/fleximg/image/viewport.inl"

// Operations
#include "../../impl/fleximg/operations/filters.inl"

// Nodes
#include "../../impl/fleximg/nodes/affine_node.inl"
#include "../../impl/fleximg/nodes/composite_node.inl"
#include "../../impl/fleximg/nodes/distributor_node.inl"
#include "../../impl/fleximg/nodes/filter_node_base.inl"
#include "../../impl/fleximg/nodes/horizontal_blur_node.inl"
#include "../../impl/fleximg/nodes/matte_node.inl"
#include "../../impl/fleximg/nodes/ninepatch_source_node.inl"
#include "../../impl/fleximg/nodes/renderer_node.inl"
#include "../../impl/fleximg/nodes/sink_node.inl"
#include "../../impl/fleximg/nodes/source_node.inl"
#include "../../impl/fleximg/nodes/vertical_blur_node.inl"
