# /ntfy — ntfy リスナー管理

ntfy 双方向メッセージングの有効化・リスナー管理を行う。

## セッション開始時のセットアップ

```bash
# 1. 有効化
touch ~/.claude/hooks/.ntfy-enabled

# 2. リスナー起動
bash ~/.claude/hooks/ntfy-listener.sh start
```

## コマンド一覧

| コマンド | 用途 |
|---------|------|
| `touch ~/.claude/hooks/.ntfy-enabled` | ntfy を有効化 |
| `rm ~/.claude/hooks/.ntfy-enabled` | ntfy を無効化 |
| `bash ~/.claude/hooks/ntfy-listener.sh start` | リスナー起動 |
| `bash ~/.claude/hooks/ntfy-listener.sh stop` | リスナー停止 |
| `bash ~/.claude/hooks/ntfy-listener.sh status` | リスナー状態確認 |

## トピック

トピック名は `~/.claude/hooks/ntfy-config.env` の `NTFY_TOPIC` / `NTFY_REPLY_TOPIC` で定義されている。スマホの ntfy アプリで該当トピックを購読すること。

## 注意事項

- hooks は Claude Code セッション起動時にスナップショットされる。`~/.claude/settings.json` の hooks を変更した場合は新セッションが必要
- リスナーはバックグラウンドプロセスとして動作する。tmux セッション終了時に自動停止する
- 設定ファイル: `~/.claude/hooks/ntfy-config.env`
