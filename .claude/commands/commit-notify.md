# /commit-notify — コミット後通知フロー

maker がコミット・push・PR 作成後にチームメイトに通知する定型手順。

## コミット完了時

`reviewer` と `steward` に SendMessage で通知する。

**reviewer 宛**: レビュー依頼
- コミットハッシュと変更ファイル一覧を含める
- diff の確認方法（`git show <hash>`）を添える

**steward 宛**: ドキュメント整合確認依頼
- 変更ファイル一覧を含める
- CLAUDE.md に影響がある場合は明示する

## push + PR 作成後

`steward` に SendMessage でプロセスチェック依頼を送る:
- PR 番号と URL を含める
- CI・Copilot レビュー・マージ条件の確認を依頼する

`team-lead` にも PR 作成完了を報告する:
- PR URL とレビュー状況（reviewer LGTM 済み等）を含める
