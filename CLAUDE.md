# Claude Code プロジェクトガイドライン

## プロジェクト概要

**fleximg**: ノードベースの画像処理パイプラインライブラリ
- C++17（一部C++14互換）、組み込み環境対応
- WebAssembly版デモあり（GitHub Pages公開中）
- M5Stack等のマイコン向けサンプルあり

### 主要ディレクトリ

```
src/fleximg/           # 公開ヘッダ（宣言のみ）
├── nodes/             # ノード宣言
├── image/             # ImageBuffer, PixelFormat, ViewPort
├── operations/        # filters, transform
└── core/memory/       # アロケータ、プール管理
impl/fleximg/          # 実装ファイル（.inl、非公開）
├── nodes/             # ノード実装
├── image/             # PixelFormat, ViewPort 実装
├── operations/        # filters 実装
└── core/memory/       # メモリ管理実装
examples/              # サンプルコード
├── bench/             # ベンチマーク（native/M5Stack両対応）
├── m5stack_basic/     # M5Stack基本サンプル
└── m5stack_matte/     # マット合成サンプル
demo/                  # WebAssemblyデモ
test/                  # doctestベースのテスト
docs/                  # 設計ドキュメント
```

### 参照すべきドキュメント

- `docs/README.md` - ドキュメント一覧
- `docs/ARCHITECTURE.md` - 全体アーキテクチャ
- `TODO.md` - 課題・アイデア管理
- `CHANGELOG.md` - 変更履歴

## ブランチ運用規則

### 命名規則
- 作業用ブランチは `claude/` で始める名前を使用する
- 形式: `claude/<作業内容>`

### ワークフロー
0. **セッション開始時に `git status` を確認し、未コミットの変更がある場合はユーザーに確認する**
   - 必要に応じて `git stash push -m "説明"` で退避してから作業を開始
   - 異なる内容の作業は別ブランチ・別セッションで行うことを推奨
1. mainブランチから新しい作業用ブランチを作成
2. 作業量が多い場合は `.work/plans/` に作業計画書を作成する
3. 作業のフェーズ単位で、作業計画書の更新とコミットを行う
4. 作業完了後、必要に応じてドキュメントを更新
5. 作業フェーズ単位のコミットを適切な粒度にまとめてからPRを作成する
6. PRのマージ作業は、ユーザー側が確認して実施する
7. マージが確認されたら、作業用ブランチと作業計画書を削除する

### 作業用フォルダ構成
`.work/` は `.gitignore` に含まれるローカル専用フォルダ

```
.work/
├── plans/    # 作業計画書（Claude用、セッション継続時に参照）
└── reports/  # 分析レポート等（ユーザー用）
```

### コンテキスト継続
セッション継続時（`/compact` 後や新規セッション開始時）、`.work/plans/` 内のドキュメントを確認すること

## ビルド手順

### WebAssembly（Emscripten）

```bash
source /path/to/emsdk/emsdk_env.sh  # Emscripten環境を読み込み
./build.sh                          # リリースビルド（8bit Straight）
./build.sh --debug                  # デバッグビルド
./build.sh --premul                 # 16bit Premulモード
```

### PlatformIO

```bash
pio run -e bench_native             # ネイティブベンチマーク
pio run -e basic_m5stack_core2 -t upload  # M5Stack書き込み
```

### テスト

```bash
pio test -e test_native             # PlatformIOでテスト実行（推奨）
cd test && make test                # Makefileでテスト実行（従来方式）
```

GitHub Actions CI（`.github/workflows/test.yml`）で main への push / PR 時に自動テストが実行される。

※ローカル環境固有の設定（emsdkパスなど）は `CLAUDE.local.md` に記載

## コード品質

- 変更前に必ずビルドが通ることを確認
- TODO.md で課題を管理
- コミットメッセージは変更内容を明確に記載

## コーディング規約

詳細は `docs/CODING_STYLE.md` を参照。以下は特に重要なポイント:

- **警告オプション**: `-Wall -Wextra -Wpedantic` でクリーンビルドを維持
- **memcpy/memset**: サイズ引数は `static_cast<size_t>(...)` でキャスト
- **ループカウンタ**: 終端変数と型を一致させる
- **配列インデックス**: 型が異なる場合は明示的キャスト
- **フォーマッタ**: `.clang-format`（M5Stack準拠）を配置済み
