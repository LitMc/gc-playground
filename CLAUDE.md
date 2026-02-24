# CLAUDE.md

## リポジトリ概要

GCコントローラ（Joybus）通信を RP2040（Raspberry Pi Pico）で解析・変換するための実験リポジトリ。
現代環境（Switch 2 の GameCube Classics 等）で生じる入力変換の差分を、外付けブリッジで補正することが最終目標。

このリポジトリは「単一アプリ」ではなく、学習と並行して前進するための**独立した小さなサブプロジェクト群（examples/）の集合**である。

## ブランチ運用と PR ワークフロー

- **main への直接 push は禁止**。必ずフィーチャーブランチを切って PR 経由でマージする。
- ブランチ命名規則: `<種別>/<内容>`（例: `fix/measure-timing`, `add/new-example`, `improve/barcode-decode`）
- PR は `gh pr create` で作成する。タイトル・本文は日本語で記述する。
- CI（build-all-examples）が通ることを確認してからマージする。

### PR の作業フロー

1. **ブランチを切って作業**: `git checkout -b <種別>/<内容>`
2. **commit を重ねる**: 日本語コミットメッセージで意図を記述
3. **push して PR を作成**: `gh pr create` でタイトル・本文を日本語で記述
4. **Copilot レビューへの対応**:
   - PR への push ごとに Copilot のレビューコメントを `gh api` で確認する
   - Copilot が提案 PR を作成した場合は内容をレビューする
     - 有用な変更があればマージし、リモートブランチを削除する
     - 変更が不要・空であればクローズし、リモートブランチを削除する
   - レビュー指摘に基づき本体 PR を修正し、commit & push する
5. **PR 概要の更新**: push のたびに `gh pr edit` で PR 本文を最新の変更内容に合わせて更新する
6. **マージ**: 懸念点がすべて解消され、CI チェックに通り次第 `gh pr merge --merge --delete-branch` でマージしリモートブランチを削除する
7. **ローカル反映**: マージ後 `git checkout main && git pull` で最新を取得する

## ディレクトリ構成

| ディレクトリ | 役割 |
|---|---|
| `examples/` | 独立した Pico ファームウェア実験（1テーマ = 1ターゲット） |
| `tools/` | Python スクリプト群（動画処理・可視化・データ化） |
| `tools/measurement_lib/` | Python ツール間の共通ライブラリ |
| `resources/` | 動画、ROI 定義、テンプレート画像、計測 CSV 等の素材 |
| `docs/` | 設計背景・構成・計測手順 |

## 参照ドキュメント

変更や質問の前に、該当するドキュメントを参照すること:

- `docs/project_overview.md` — 目的・背景・成果物のイメージ
- `docs/repo_structure.md` — なぜこの構成か（「2回コピペしたら共通化」ルール）
- `docs/hardware.md` — 配線・電源（OR 回路）・ピン割当
- `docs/measurements.md` — 計測フロー全体（バーコード形式、ROI dump、テンプレートマッチ、CSV 生成）
- `docs/transforms.md` — 補正変換パイプライン（S, S⁻¹, C, φ の数学的定義と可視化）

## 技術スタック

### C++（Pico ファームウェア）

- C++20 / CMake / pico-sdk
- ビルド: `cmake -B build -S . && cmake --build build -j`
- ターゲット指定: `cmake --build build --target <target>`
- フラッシュ: `cp build/examples/<target>/<target>.uf2 /Volumes/RPI-RP2/`
- フォーマット: `.clang-format` に従う（LLVM ベース、インデント 4、100 桁制限）

### Python（解析ツール）

- Python 3.13 / uv で管理
- 実行: `uv run tools/<script>.py <args>`
- 依存追加: `uv add <package>`
- フォーマット: `.editorconfig` に従う（UTF-8、LF、末尾改行）

## コーディング上の重要制約

- Joybus はタイミングが厳密。ISR 内で重い処理・動的確保・ブロッキング I/O をしない。
- ログや解析出力は「必要最小限」「落としても動作が壊れない」を優先する。
- 既存のテスト/計測手順（`docs/measurements.md`）を壊さない変更を優先する。
- `examples/` 内の各ターゲットは単体でビルド可能であること。
- 新しい実験は `examples/<topic>/` を作り、ルートの `CMakeLists.txt` に `add_subdirectory` を追加する。
- 共通化は「2回以上コピペしたら検討」くらいの温度感で行う（早すぎる共通化は避ける）。

## 変更の検証

- C++ ファームウェア: `cmake --build build -j` で全ターゲットビルドが通ること
- 特定ターゲット: `cmake --build build --target <target>`
- Python ツール: `uv run tools/<script>.py --help` で起動確認
- CI 相当: GitHub Actions の build-all-examples ワークフローと同等の検証

## コミットルール

- 日本語のコミットメッセージを使用
- 変更内容と意図を簡潔に記述
- 例: `measure の送信パターンに CRC チェックを追加`、`dump_measurement_rois にフレーム指定オプションを追加`

## Entire 連携

このリポジトリは [Entire](https://entire.io) の **manual-commit** 戦略で運用する。

Entire はセッションごとにプロンプト・作業要約・コンテキストを記録し、hooks を通じて過去の記録を提供することがある。

### 活用ルール

- hooks から提供される過去のセッション情報（作業履歴・決定事項・コンテキスト）は積極的に参照し、作業の継続性を保つこと
- Entire が質の高い記録を蓄積できるよう、以下を心がけること：
  - コミットメッセージに変更の意図と背景を含める
  - PR 本文に変更の目的・影響範囲を記述する
  - 設計判断や代替案を検討した場合、その経緯を PR 本文やコミットに残す

### 制約

- `.claude/settings.json` の hooks は Entire が管理している。**内容を変更・削除しないこと**。
- `.entire/` 配下のファイルは Entire が自動生成する。**手動で編集しないこと**。

## 触れてはいけないファイル

以下のファイル・ディレクトリはツールが管理しているため、手動で変更しないこと。

- `.claude/settings.json` — Entire が管理する hooks 設定
- `.entire/` — Entire の作業ディレクトリ（設定・ログ・一時ファイル）

## ドキュメント保守ルール

コードを変更する際は、以下のドキュメント同期を確認すること:

- **CLAUDE.md**: ワークフロー・制約・ディレクトリ構成に変更があれば更新する
- **docs/**: 対応する設計ドキュメントの記述が変更内容と矛盾しないか確認する
  - `examples/` の追加・削除 → `docs/repo_structure.md`
  - 配線・ピン割当の変更 → `docs/hardware.md`
  - 計測パイプラインの変更 → `docs/measurements.md`
  - プロジェクト目標・背景の変更 → `docs/project_overview.md`
  - 補正変換パイプラインの変更 → `docs/transforms.md`
- **README.md**: ビルド手順・クイックスタートに影響する変更があれば更新する
- **CMakeLists.txt**: `examples/` の追加時は `add_subdirectory` を追加する
- **.github/copilot-instructions.md**: CLAUDE.md と矛盾する記述が生じないよう同期する
