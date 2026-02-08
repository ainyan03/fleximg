# マイグレーションガイド

fleximg の API 変更と非推奨 API からの移行手順を説明します。

## 現在のアクティブなメジャーバージョン

- **v2.x**: 現在のメインブランチ（安定版）
- **v3.0 (計画中)**: 破壊的変更により後方互換性を廃止予定

## 非推奨 API

### 1. グローバルスコープの型エイリアス（v3.0 で削除予定）

**非推奨:**
```cpp
using AffineMatrix = core::AffineMatrix;          // types.h
using AffineCapability = core::AffineCapability;  // types.h
using PerfMetrics = core::PerfMetrics;            // perf_metrics.h
```

**理由:**
- 名前空間の明確性を高める
- `core::` スコープに統一し、API の構造を明確化
- グローバルスコープのポリューション回避

**移行方法:**
```cpp
// Before (非推奨)
namespace fimg = fleximg;
using AffineMatrix = fimg::AffineMatrix;

// After (推奨)
namespace fimg = fleximg;
using AffineMatrix = fimg::core::AffineMatrix;
```

より詳しく：

#### types.h（L276 周辺）

```cpp
// 削除対象（v3.0）
[DEPRECATED] using AffineMatrix = core::AffineMatrix;
[DEPRECATED] using Point = core::Point;
[DEPRECATED] using AffineCapability = core::AffineCapability;
```

**移行:**
- `AffineMatrix` の場合: `core::AffineMatrix` に変更
- `Point` の場合: `core::Point` に変更
- `AffineCapability` の場合: `core::AffineCapability` に変更
- または `using` ステートメントで再定義（推奨）

#### perf_metrics.h（L292 周辺）

```cpp
// 削除対象（v3.0）
[DEPRECATED] using PerfMetrics = core::PerfMetrics;
[DEPRECATED] using PerfSegment = core::PerfSegment;
```

**移行:**
- `PerfMetrics` の場合: `core::PerfMetrics` に変更
- `PerfSegment` の場合: `core::PerfSegment` に変更

## 段階的な削除スケジュール

### v2.x（現在）
- 非推奨 API はコンパイラ警告として報告
  ```cpp
  [[deprecated("Use core::AffineMatrix instead")]]
  using AffineMatrix = core::AffineMatrix;
  ```
- 既存コードは動作継続

### v2.1 - v2.9
- コンパイラ警告の継続
- 移行時間を提供（推奨）

### v3.0
- 非推奨 API を完全削除
- グローバルスコープの型エイリアスなし
- 既存コードはコンパイルエラー

## マイグレーション例

### パターン 1: グローバルを `core::` スコープに統一

```cpp
// Before (v2.x)
struct MyNode : public Node {
  AffineMatrix matrix_;  // types.h での using エイリアス
};

// After (v3.0 compatible)
struct MyNode : public Node {
  core::AffineMatrix matrix_;  // 明示的な core:: スコープ
};
```

### パターン 2: ローカル using で統一

```cpp
// Before (v2.x)
#include "fleximg/fleximg.h"
using AffineMatrix = fleximg::AffineMatrix;  // グローバルエイリアス

// After (v3.0 compatible)
#include "fleximg/fleximg.h"
namespace fimg = fleximg;
using AffineMatrix = fimg::core::AffineMatrix;  // 名前空間明示的
```

### パターン 3: ノードの定義

```cpp
// Before (v2.x、コンパイラ警告あり)
fimg::AffineMatrix mtx;
mtx.setIdentity();

// After (v3.0 compatible)
fimg::core::AffineMatrix mtx;
mtx.setIdentity();
```

## 移行チェックリスト

- [ ] コンパイラ警告を確認（`-Wdeprecated-declarations` オプション）
- [ ] グローバルスコープの `using` ステートメントを確認
- [ ] `AffineMatrix`、`Point` の使用箇所を `core::` スコープに変更
- [ ] `PerfMetrics`、`PerfSegment` の使用箇所を `core::` スコープに変更
- [ ] テストビルド & 実行

## コンパイル警告の有効化

非推奨 API 使用箇所を発見するには、以下のコンパイラオプションで警告を有効化してください：

**GCC/Clang:**
```bash
-Wall -Wextra -Wdeprecated-declarations
```

**PlatformIO:**
```ini
[common_native]
build_flags =
    -Wall -Wextra -Wpedantic
    -Wdeprecated-declarations
```

## FAQ

### Q: 古いコードを動かし続けたい

**A:** v2.x を使い続けてください。v3.0 が released される前に、対象バージョンを指定できます。

### Q: 移行に何文字修正が必要？

**A:** グローバルスコープの `using` を削除し、各使用箇所に `core::` を追加するだけです。
例: `AffineMatrix` → `core::AffineMatrix`（8 文字追加）

### Q: v3.0 の release 予定は？

**A:** 2026 年中ごろを予定しています。
確定情報は [TODO.md](TODO.md) 確認してください。

## 関連リソース

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) - アーキテクチャ概要
- [CODING_STYLE.md](docs/CODING_STYLE.md) - コーディング規約
- [CHANGELOG.md](CHANGELOG.md) - 変更履歴
