# リモート開発環境

出先から macOS 環境へ安全にアクセスし、gc-playground の開発を継続するための環境。
Tailscale（メッシュ VPN）+ macOS SSH + tmux の組み合わせで構築済み。

## アーキテクチャ

```
[Android / Termux]
    ↓ Tailscale VPN（WireGuard、NAT越え・ポート開放不要）
[<hostname>.tail<id>.ts.net : 22]
    ↓ SSH
[macOS: tmux セッション]
    ↓
[claude / vim / git / cmake など]
```

## 構築済みコンポーネント

| コンポーネント | 詳細 |
|--------------|------|
| Tailscale IPv4 | `tailscale ip` で確認 |
| MagicDNS ホスト名 | `tailscale status` で確認 |
| SSH Remote Login | 有効（ポート 22） |
| caffeinate LaunchAgent | `~/Library/LaunchAgents/com.user.caffeinate.plist` で常駐 |
| pmset (AC電源時) | `sleep=0`, `disksleep=0` |

## 日常の接続手順

```bash
# 1. Tailscale が繋がっていることを確認（自動接続）
tailscale status

# 2. SSH（MagicDNS ホスト名または IP）
ssh jgoto@<hostname>.tail<id>.ts.net

# 3. 作業セッションを復元
tmux attach -t work
# なければ新規作成
tmux new -s work

# 4. Claude Code を起動
cd ~/pico/gc-playground && claude
```

## セットアップ手順（再構築用）

### 1. Tailscale インストール

```bash
brew install tailscale

# ⚠️ root 権限が必要なため sudo を付けること
# `brew services start tailscale`（sudo なし）は起動エラーになる
sudo brew services start tailscale

# ブラウザが開くのでアカウントにログイン
tailscale up

# IP とホスト名を確認
tailscale ip
tailscale status
```

### 2. SSH Remote Login を有効化

```bash
sudo systemsetup -setremotelogin on
sudo systemsetup -getremotelogin   # "Remote Login: On" が返ればOK
```

または **System Settings > General > Sharing > Remote Login** をオン。

### 3. スリープ防止の永続化

AC 電源接続時にスリープしないよう pmset で永続設定する。

```bash
sudo pmset -c sleep 0        # AC 電源時: システムスリープ無効
sudo pmset -c disksleep 0    # AC 電源時: ディスクスリープ無効
pmset -g | grep sleep        # 確認
```

加えて caffeinate を LaunchAgent で常駐させる:

```bash
# ~/Library/LaunchAgents/com.user.caffeinate.plist を作成
cat > ~/Library/LaunchAgents/com.user.caffeinate.plist << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.user.caffeinate</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/caffeinate</string>
        <string>-s</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
EOF

launchctl load ~/Library/LaunchAgents/com.user.caffeinate.plist
launchctl list | grep caffeinate   # PID が表示されれば稼働中
```

> `caffeinate -s` は AC 電源接続中のシステムスリープのみを防止する。ディスプレイスリープは許容。

### 4. Android 側のセットアップ

**Tailscale**（Google Play）:
- 同じアカウントでログイン → 自動的に同一 VPN ネットワーク内に入る

**Termux**（[F-Droid 版](https://f-droid.org/packages/com.termux/) 推奨）:

```bash
pkg update && pkg upgrade
pkg install openssh

# パスワードレス接続の設定（推奨）
ssh-keygen -t ed25519 -C "android-termux"
ssh-copy-id jgoto@<tailscale-ip>
```

> Hacker's Keyboard（Google Play）を使うと Ctrl/Esc/矢印キーが打ちやすくなる。

## トラブルシューティング

| 症状 | 原因 | 対処 |
|------|------|------|
| `failed to connect to local Tailscale service` | `tailscaled` がユーザー権限で起動している（root が必要） | `brew services stop tailscale` → `sudo brew services start tailscale` |
| SSH 接続できない | Remote Login が無効、または pmset でスリープした | `sudo systemsetup -setremotelogin on` を確認。スリープ設定を再確認 |

## セキュリティ

- Tailscale は WireGuard ベースで E2E 暗号化済み。macOS の SSH ポート (22) は Tailscale ネットワーク内にのみ公開される
- SSH は鍵認証のみに限定することを推奨（`/etc/ssh/sshd_config` で `PasswordAuthentication no`）
- Tailscale アカウントへの 2FA 設定を推奨
