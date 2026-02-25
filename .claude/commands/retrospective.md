# /retrospective — 振り返りと改善提案

作業後の振り返り（retrospective）を実施し、SKILL 化・エージェント更新・ドキュメント更新の改善候補を提案する。

## 担当分担

| 担当 | 役割 |
|------|------|
| facilitator | 振り返り主担当（PR 視点・全体の集約） |
| navigator | 実験設計パターン観察（補助） |
| guardian | ドキュメント変更の実施（承認後） |
| implementer | コード系変更の実施（承認後） |

## フロー

### 1. 事実収集（facilitator が実施）

```bash
git log --oneline -20
gh pr list --state merged --limit 10
```

Read で確認:
- `CLAUDE.md` の「エージェント改善履歴」セクション
- `.claude/agents/` 配下のエージェント定義
- `.claude/commands/` 配下の SKILL 定義

### 2. パターン観察

| 観点 | 基準 |
|------|------|
| **SKILL化候補** | 3回以上同じ手順を繰り返した |
| **エージェント更新候補** | 役割の迷い・ツール不足・定義と実際の動きの乖離 |
| **ドキュメント更新候補** | 手順書と実際の作業が乖離している |

navigator が実験設計の観点でパターンを補足報告する（オプション）。

### 3. 改善候補リストを team-lead に提示

以下の3区分で整理して報告する:

```
## 振り返り結果

### SKILL化候補
- [ ] <手順の説明>: <なぜ SKILL 化すべきか>

### エージェント更新候補
- [ ] <エージェント名>: <何を変更すべきか・なぜか>

### ドキュメント更新候補
- [ ] <ドキュメントファイル>: <乖離内容>

### 変更不要
（観察なし、または変化が小さい場合）
```

### 4. team-lead の承認を得てから実施（facilitator は追跡のみ）

- **承認なしでの変更は禁止**
- **facilitator 自身はファイルを変更しない。** 担当エージェントに依頼する。
- SKILL 追加: `guardian` または `implementer` が team-lead 承認後に `.claude/commands/<name>.md` を新規作成（facilitator は提案のみで直接編集しない）
- エージェント更新: `guardian` または `implementer` が team-lead 承認後に `.claude/agents/<name>.md` を修正（facilitator は提案のみで直接編集しない）
- CLAUDE.md 更新: guardian が「エージェント改善履歴」に日付・理由を記録

### 5. 改善履歴に記録

`CLAUDE.md` の「エージェント改善履歴」に以下の形式で追記:

```
- YYYY-MM-DD: <変更内容の要約>（/retrospective で発見、team-lead 承認済み）
```

## 呼び出し方

- **自動**: guardian が PR マージ完了後に facilitator へ通知 → facilitator が自動起動
- **手動**: `/retrospective` をコマンドとして呼ぶ（team-lead またはユーザーが指示）
- **補助トリガー**: navigator がパターン報告 → facilitator が振り返りに組み込む
