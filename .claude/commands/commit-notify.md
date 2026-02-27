# /commit-notify — コミット後通知フロー

maker がコミット・push・PR 作成後にチームメイトに通知する定型手順。

## コミット完了時

`reviewer` に SendMessage でレビュー依頼を送る:
- コミットハッシュと変更ファイル一覧を含める
- diff の確認方法（`git show <hash>`）を添える

## push + PR 作成後

`team-lead` に SendMessage で PR 作成完了を報告する:
- PR 番号と URL を含める
- CI ステータスと Copilot レビュー状況を含める
- マージ条件 3 点の充足状況を報告する
