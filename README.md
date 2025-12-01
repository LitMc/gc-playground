# gc-playground — Raspberry Pi Pico 学習用リポジトリ

このリポジトリは、Raspberry Pi Pico を用いた C/C++ 学習用プロジェクトです。`pico-sdk` を利用し、LED 点灯や PIO の基本などのサンプルが含まれます。

主なサンプル:
- `examples/led_onboard`: Pico のオンボード LED を制御
- `examples/led_ext`: 外部 LED を GPIO で制御
- `examples/led_ext_pio`: PIO を用いた LED 制御
- `examples/led_ext_pio_auto`: PIO 自動生成フローを用いた LED 制御

## 前提条件
- macOS
- CMake 3.13+（Homebrew 推奨）
- Ninja（任意）
- ARM GCC ツールチェーン（`arm-none-eabi-gcc` など）
- USB ケーブル（データ通信用）

Homebrew の例:
```fish
brew install cmake ninja
# ARM ツールチェーンは環境に応じてインストール（例: brew install arm-none-eabi-gcc）
```

このリポジトリには `pico-sdk/` が同梱されています（`pico_sdk_import.cmake` 経由で参照）。もし外部の SDK を使う場合は、環境変数 `PICO_SDK_PATH` を設定してください。
```fish
# 例: SDK を別ディレクトリに置く場合
set -Ux PICO_SDK_PATH /path/to/pico-sdk
```

## ビルド方法
初回ビルド:
```fish
cmake -B build -S .
cmake --build build -j
```
特定ターゲットのみビルド:
```fish
# 例: led_onboard のみ
cmake --build build --target led_onboard
```
生成物の例:
- `build/examples/led_onboard/led_onboard.uf2`
- `build/examples/led_ext/led_ext.uf2`
- `build/examples/led_ext_pio/led_ext_pio.uf2`
- `build/examples/led_ext_pio_auto/led_ext_pio_auto.uf2`

## 書き込み（フラッシュ）方法
### 方法 A: BOOTSEL マスストレージへコピー
1. Pico の BOOTSEL ボタンを押したまま USB 接続
2. マウントされたドライブ（`/Volumes/RPI-RP2`）へ `.uf2` をコピー
```fish
cp build/examples/led_onboard/led_onboard.uf2 /Volumes/RPI-RP2/
```

### 方法 B: `picotool` を使用
このプロジェクトはビルド時に `picotool` を取得・ビルドします（`build/_deps/picotool-build/picotool`）。BOOTSEL モードで以下を実行:
```fish
# UF2 を書き込み
build/_deps/picotool-build/picotool load -f build/examples/led_onboard/led_onboard.uf2

# 情報確認
build/_deps/picotool-build/picotool info
```

## 動作確認の例
オンボード LED:
```fish
cmake --build build --target led_onboard
cp build/examples/led_onboard/led_onboard.uf2 /Volumes/RPI-RP2/
```
書き込み後に Pico が自動で再起動し、LED が点滅します。

## PIO 関連ツール
`pioasm` はビルドに含まれており、PIO プログラム（`.pio`）のアセンブルに利用されます。

## お掃除（クリーンビルド）
```fish
rm -rf build
```

## ディレクトリ構成（抜粋）
```
CMakeLists.txt
pico_sdk_import.cmake
examples/
  led_onboard/
  led_ext/
  led_ext_pio/
  led_ext_pio_auto/
pico-sdk/
```

## トラブルシューティング
- ツールチェーンが見つからない: `arm-none-eabi-gcc` のインストールを確認
- SDK が見つからない: `pico-sdk/` が存在するか、`PICO_SDK_PATH` を設定
- BOOTSEL モードに入らない: BOOTSEL を押したまま USB 接続、電源供給を確認

## ライセンス
学習用コンテンツです。各サブプロジェクトや依存物（例: `pico-sdk`, `picotool`）のライセンスはそれぞれのリポジトリに従います。
