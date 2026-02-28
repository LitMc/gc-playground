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
   - **2回目以降の push 後は `gh pr comment <number> --body "@copilot re-review"` でレビューを依頼する**（初回のみ自動、以降は手動）
   - **指摘の採否は Teams で判断する**: reviewer が技術的妥当性を評価し、maker がプロセス整合を確認する
     - 有効な指摘は本体 PR に反映し commit & push する
     - 無効・誤認識（例: 存在しないパスへの言及）は Teams 判断でスキップ可
   - Copilot が**フィーチャーブランチに対して**提案 PR を作成した場合は **Teams がレビューしてマージまたはクローズ**する（maker 主担当、ユーザー承認不要）
     - 有用な変更があればマージし、リモートブランチを削除する
     - 変更が不要・空であればクローズし、リモートブランチを削除する
   - ※ Teams 自身が作成したフィーチャーブランチの PR のマージは、必ずユーザーの承認を得ること（ステップ6参照）
5. **PR 概要の更新**: push のたびに `gh pr edit` で PR 本文を最新の変更内容に合わせて更新する
6. **ユーザーの最終承認を得る**: 以下の**マージ条件3点**が全て満たされたうえで、**必ずユーザーに「マージしてよいですか？」と確認する**。エージェントが自律的にマージすることは禁止。承認後に新コミットが加わった場合は、**承認を取り直す**こと。
   - CI（build-all-examples）が通っていること
   - Copilot のレビュー指摘に対応できていること（指摘がない場合も `gh api` で確認済みであること）
   - Copilot の提案 PR がマージまたはクローズされていること
7. **マージ**: 承認を得たうえで `gh pr merge --merge --delete-branch` でマージしリモートブランチを削除する
8. **ローカル反映**: マージ後 `git checkout main && git pull` で最新を取得する
9. **振り返り**: team-lead（または自分）が `/retrospective` を実行する（PR の大小に関わらず毎回実施）

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

## カスタムエージェントと Agent Teams

Claude Code の複数エージェント機能を活用して、並行・協調作業を行う。

### 有効化状態

- **カスタムサブエージェント**: `.claude/agents/` に定義済み（Task tool 経由で動作）
- **Agent Teams（実験的）**: `~/.claude/settings.json` で `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1` を設定済み

### チーム作業の開始フロー（work initiation）

ユーザーから実装・変更を伴う作業依頼を受けたとき、team-lead は以下の手順で動く:

> **前提条件**: 小規模（タイポ修正・設定変更等、明らかに軽微な作業）は maker のみ（Teams 不要）。それ以外は常にフル 3 体（maker + reviewer + challenger）で TeamCreate して開始する。詳細は [`.claude/AGENT_TEAMS.md`](.claude/AGENT_TEAMS.md) の「タスク規模に応じたスポーン方針」を参照。

1. **タスクコンテキスト整理**: タスクを分解し、コンテキスト（背景・目的・制約）を整理する
2. **エージェントスポーン + plan 提案依頼（コンペ方式）**: 3体を一括スポーンし、コンテキストと共に plan 提案を依頼する
3. **討論ラウンド**: team-lead が全提案を共有し、エージェント間で批評・説得・改善を議論する
4. **plan 統合**: team-lead が討論を踏まえて最終 plan を策定する

#### team-lead の禁止事項

以下の作業は**必ず他エージェントに委譲し、team-lead 自身は行わない**:

| 禁止事項 | 委譲先 |
|---------|--------|
| ファイル作成・編集 | maker |
| コミット | maker |
| push・PR 作成・CI 確認・Copilot レビュー確認 | maker |
| コードレビュー・タイミング評価・設計妥当性の評価 | reviewer |
| 影響範囲の自律探索・前提への問い | challenger |

**例外**: 純粋な読み取り・ユーザーへの説明・plan の統合（コンペ方式での最終策定）は team-lead が行う。

#### タスク種別とデフォルト担当

| タスク種別 | 担当エージェント |
|-----------|---------------|
| C++ / Python コード実装 | maker |
| ファイル編集・コミット（ドキュメント含む） | maker |
| push・PR 作成・マージ・CI 確認・Copilot レビュー確認 | maker |
| コードレビュー（タイミング/ISR/品質） | reviewer |
| 設計妥当性・実験設計の評価・Copilot 指摘の技術的採否 | reviewer |
| 影響範囲の自律探索・前提への問い・代替案の軽量調査 | challenger |
| plan 提案（各自の視点から独立に） | maker + reviewer + challenger（コンペ方式） |
| 振り返り・改善提案 | team-lead |
| 純粋な読み取り・ユーザーへの説明 | team-lead |

### カスタムサブエージェント一覧（3体構成）

機能ベースで再編し、ハンドオフを最小化する設計。エージェント間で直接対話しながら品質を高める。

| エージェント | 機能 | 用途 |
|------------|------|------|
| `maker` | 「作る」 | ファイル編集・ビルド・コミット・push・PR 作成・CI/Copilot 確認まで end-to-end で担当 |
| `reviewer` | 「見る」 | コードレビュー・タイミング安全性・設計評価・Copilot 指摘の技術的採否（読み取り専用） |
| `challenger` | 「問う」 | 影響範囲の自律探索・前提への問い・代替案の軽量調査（読み取り専用） |

定義ファイル: `.claude/agents/<name>.md`

### カスタム SKILL 一覧

| コマンド | 用途 |
|---------|------|
| `/skill-new` | 新しい SKILL を `.claude/commands/` に追加するフローを案内する |
| `/agents-review` | エージェント構成を見直し、改善案を提案・適用する |
| `/retrospective` | 作業後の振り返りを行い、SKILL化・エージェント更新の改善案を提案する |
| `/commit-notify` | コミット・PR 作成後のチームメイト通知手順 |
| `/ntfy` | ntfy リスナーの管理（起動・停止・状態確認） |

> Agent Teams の詳細設定（使い方・作業フロー・mode 指針・モデル選択ルール・自己改善・ベストプラクティス等）は [`.claude/AGENT_TEAMS.md`](.claude/AGENT_TEAMS.md) を参照。

### エージェント改善履歴

- 2026-02-26: team-lead を委譲専門に特化。work initiation フロー（plan → facilitator レビュー → スポーン）を新設。facilitator の役割を振り返り専門から plan レビュー主担当に拡張。
- 2026-02-26: PR #57 振り返り: work initiation フロー必須化を明記、navigator にリファクタリング時の設計意図確認を追加、facilitator の plan レビュー観点を拡充、/retrospective に全エージェントヒアリングを追加（前半の運用ルール違反と active_pad_hub() 誤削除の教訓）
- 2026-02-26: モデル選択ルールを CLAUDE.md に明記。Task ツールの model パラメータはユーザー指示がない限り指定しない（Opus 4.6 を継承）
- 2026-02-27: エージェント体制を刷新: 5体構成(implementer/critic/guardian/navigator/facilitator)から3体構成(maker/reviewer/steward)に再編。ハンドオフ最小化・エージェント間対話・観察ログ・自己点検を導入（/retrospective で発見、ユーザー承認済み）
- 2026-02-27: エージェント構造の軽量化: CLAUDE.md 詳細を .claude/AGENT_TEAMS.md に外部化、steward 振り返り手順を /retrospective 参照化、観察ログ SendMessage 一元化、shutdown テンプレ重複排除、規模別スポーン方針追加、ワークフロー3フェーズ化
- 2026-02-27: スポーン方針を2段階に簡略化（中規模を廃止、trivial以外は常にフル3体）。Phase 1 にコンペ方式 plan を導入（3エージェントが独立に plan を提案→討論→team-lead が統合）
- 2026-02-28: エージェント体制を改善: steward を廃止し challenger（問い手）を導入。CI/Copilot 確認を maker に統合、Copilot 指摘の技術的採否を reviewer に統合、振り返りを team-lead に移管。challenger は影響範囲の自律探索・前提への問い・中間リフレクションを担当（/retrospective で発見、ユーザー承認済み）
- 2026-02-28: /retrospective の steward 参照を team-lead に修正、/ntfy SKILL を追加（セッション開始時のリスナー管理手順を SKILL 化）。リポジトリ外のエージェント基盤（hooks 等）を別リポジトリに分離する方向性を確認（次回宿題）（/retrospective で発見、ユーザー承認済み）
