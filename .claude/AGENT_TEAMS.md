# Agent Teams 詳細設定

> このファイルは CLAUDE.md から外部化した Agent Teams の詳細設定です。
> 基本的な構成・開始フロー・エージェント一覧は [CLAUDE.md](../CLAUDE.md) を参照してください。

## Agent Teams の使い方

複数チームメイトが独立して並行作業し、直接通信する場合に使用する。

```
PR をレビューするために、セキュリティ・タイミング・コーディング規約を
担当する3人のチームメイトを作成して
```

**このプロジェクトでのユースケース**:

標準的な作業フロー:
```
team-lead → steward + maker + reviewer を一括スポーン
steward: plan を確認（問題があれば team-lead に報告）
maker ←→ reviewer: 実装しながら品質を議論
steward: 横でプロセスを監視し、逸脱があれば介入
全員が observations.md に気づきを記録
→ steward が振り返りを実施 → 全員シャットダウン
```

**並列のポイント**:
- maker と reviewer は対話しながら並行作業
- reviewer のレビューと steward のドキュメント確認は並列
- CI 待ちの間に steward は Copilot レビューも確認

### タスク規模に応じたスポーン方針

| 規模 | 基準 | スポーン |
|------|------|---------|
| 小 | ドキュメント修正・タイポ・設定変更 | maker のみ（Teams 不要、Task ツールで直接） |
| 中 | 既存コード修正・Python ツール変更 | maker + steward |
| 大 | C++ ISR変更・新example追加・アーキテクチャ変更 | maker + reviewer + steward（フル） |

**チームメイトのスポーンタイミング**:

| エージェント | スポーンタイミング | 備考 |
|------------|-----------------|------|
| steward | **作業開始時に最初にスポーン** | チーム終了まで常駐 |
| maker | 作業開始時（steward と同時） | 実装〜PR 作成まで一貫して担当 |
| reviewer | 作業開始時（steward と同時） | maker と対話しながらレビュー |
| 純粋な読み取り・説明のみ | team-lead（Teams 不要） | — |

> **重要**: 3体を一括スポーンし、イベント駆動で連携する。team-lead 自身がファイル編集・コミット・push・PR作成・CI確認を行うことは禁止。

### 作業フロー（3フェーズ）

**Phase 1（準備）**:
- team-lead がエージェントをスポーン（規模に応じて選択）
- steward が plan を確認（問題があれば team-lead に報告）
- maker が準備開始（並列）

**Phase 2（実装〜PR）**:
- maker が実装。reviewer と対話しながら品質を高める
- maker がコミット → reviewer にレビュー依頼 + steward に通知
- reviewer がレビュー → maker にフィードバック（steward にも共有）
- steward がドキュメント整合を確認（reviewer と並列）
- maker が push + PR 作成 → steward に通知

**Phase 3（完了）**:
- steward が CI・Copilot・マージ条件を確認 → team-lead に報告
- team-lead がユーザーに承認確認 → maker にマージ指示
- maker がマージ + ローカル反映
- steward が振り返り実施 → 全員シャットダウン → チーム削除

**観察ログの仕組み**:

maker / reviewer は気づきを steward に SendMessage で共有する。steward が `~/.claude/teams/{team-name}/observations.md` に一元記録し、振り返り時に集約して改善提案にまとめる。

**チームメイトをスポーンする際の mode 指針**:

| mode | 用途 |
|------|------|
| `bypassPermissions` | 定型作業（maker, steward）。`~/.claude/settings.json` の許可リスト（git/gh/cmake/uv）と組み合わせて使用 |
| `plan` | 新規・不確かな作業。チームメイトが計画を提示し、team-lead がレビュー・承認してから実行 |
| `default` | フォアグラウンド Task（ユーザーが直接許可プロンプトに応答できる場合） |

**チームメイトおよび Task ツールのモデル選択ルール**:

- **デフォルトモデル: `claude-opus-4-6`（Opus 4.6）**。Task ツールでは通常 `model` パラメータを指定しない（親セッションのモデル設定を継承する）
- カスタムエージェント（`.claude/agents/*.md`）は全て `model: claude-opus-4-6` で統一済み
- `model: "sonnet"` や `model: "haiku"` の明示指定は**ユーザーが明示的にコスト削減・速度優先を指示した場合のみ**許可
- 調査・探索目的の Task であっても、指示がない限り `model` パラメータを指定しないこと

> **注意**: バックグラウンドエージェント（`run_in_background: true`）は許可プロンプトを届けられない。
> バックグラウンドで使う場合は `bypassPermissions` を指定するか、`permissions.allow` でカバーすること。

**ベストプラクティス**（公式ドキュメントより）:
- チームサイズ: 3〜5人のチームメイト。チームメイトあたり 5〜6 タスク
- 同じファイルを複数チームメイトに編集させない（上書き競合が発生する）
- まず研究・レビュータスクから試す（コードを書かないタスクで感覚をつかむ）
- スポーンプロンプトにタスク固有の詳細を含める（会話履歴はチームメイトに引き継がれない）

**既知の制限**（公式ドキュメントより）:
- VSCode 統合ターミナルでは split-pane 不可（in-process モードで自動動作）
- セッション再開（`/resume`）は in-process チームメイトを復元しない
- セッションあたり 1 チームのみ。新チーム開始前に `クリーンアップして` と指示する
- ネストされたチームは不可（チームメイトが別チームをスポーンできない）

## エージェントの自己改善

エージェントは運用の中で自身の定義を改善する。

**自己改善の原則**:
- 2 回以上手動で繰り返したフローは SKILL 化を検討する（`/skill-new` を参照）
- 作業完了後にエージェント構成を振り返り、改善があれば即反映する（`/agents-review` を参照）
- 変更内容・経緯は CLAUDE.md の「エージェント改善履歴」に日付と理由を記録する

**自己改善のトリガータイミング**:
- **PR マージ後（必須）**: steward が振り返りを実施する（ワークフロー ステップ9）。observations.md を収集し、生きているエージェントにヒアリングする
- セッション終了前: ユーザーまたは team-lead が `/retrospective` を呼ぶ

**主担当**: steward（守り育てる）が振り返りと改善提案を担当する。
改善提案はいかなる場合も team-lead またはユーザーの承認後にのみ実施する。

**SKILL 化の基準**:
- 3 回以上繰り返すフロー → `.claude/commands/<name>.md` に追加
- 命名規則: 動詞-名詞（例: `build-verify`, `pr-check`, `timing-measure`）

**エージェント構成の調整ルール**:
- 役割が重複していると気づいたら統合を検討する
- tools が足りない・過剰なら即更新する
- 変更後は必ず CLAUDE.md の「エージェント改善履歴」に記録する

## エージェント改善履歴（アーカイブ）

直近の履歴は [CLAUDE.md](../CLAUDE.md) の「エージェント改善履歴」を参照。以下は過去の記録。

- 2026-02-25: 初期セット 4 エージェントを追加（cpp-builder, python-tool, pr-workflow, doc-sync）
- 2026-02-25: SKILL 2 種を追加（skill-new, agents-review）
- 2026-02-25: 全エージェントに SendMessage ツールとシャットダウン対応を追加（Agent Teams での TeamDelete がブロックされる問題を修正）
- 2026-02-25: 全エージェントのモデルを claude-opus-4-6 に統一（inherit/haiku は Agent Teams 環境で無効なため）
- 2026-02-25: ~/.claude/settings.json に permissions.allow を追加、CLAUDE.md に mode 指針とマージ前ユーザー承認ルールを追記
- 2026-02-25: 技術スタック別構成から「視点（Lens）モデル」に刷新（旧4体を廃止し implementer・critic・guardian・navigator を新設）。既存4エージェントによる並行自己レビューを経て設計を確定。
- 2026-02-25: CopilotレビューのTeams中心の運用をルール化（CLAUDE.md/guardian.md/copilot-instructions.md に明記）
- 2026-02-25: facilitator エージェント新設・/retrospective SKILL 追加・navigator に改善観察セクション追加（自己改善フィードバックループの構築）
