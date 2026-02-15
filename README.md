# gc-playground

GameCube コントローラ（Joybus）通信を、RP2040（Raspberry Pi Pico）で解析・変換するための実験リポジトリです。
最終目的は、現代環境で生じる入力変換の差分を外付けブリッジで補正できるようにすることです。

このリポジトリは「単一アプリ」ではなく、独立した小さなサブプロジェクト群で構成されています。

## まず読むドキュメント
- 目的と背景: [docs/project_overview.md](docs/project_overview.md)
- 構成の考え方: [docs/repo_structure.md](docs/repo_structure.md)
- 配線・電源注意点: [docs/hardware.md](docs/hardware.md)
- 測定手順とデータ化: [docs/measurements.md](docs/measurements.md)

## 測定ワークフロー（最短）
計測動画から `frame,sx,sy,gx,gy` を作る流れです。

1. ROI を動画から dump
```bash
uv run tools/dump_measurement_rois.py \
  --video resources/videos/2026-02-06\ 21-46-02.mp4 \
  --rois resources/rois/rois.json \
  --out-dir resources/templates/raw
```

特定フレームを一括dumpする例（1行1フレームのテキストファイルを使用）:
```bash
uv run tools/dump_measurement_rois.py \
  --video resources/videos/2026-02-06\ 21-46-02.mp4 \
  --rois resources/rois/rois.json \
  --out-dir resources/templates/raw \
  --frames-file resources/templates/frames_to_dump.txt
```

`--frames-file` の例:
```text
342
812
1222
```

dumpツール主要オプション:
- `--start-frame`: 手動モード開始フレーム
- `--frames`: `342,812,1222` のようなカンマ区切り一括指定
- `--frames-file`: 1行1フレーム番号のテキストファイル

2. dump 画像をラベリングしてテンプレート化
```bash
uv run tools/make_templates_from_dump.py \
  --raw-dir resources/templates/raw \
  --out-dir resources/templates/game_digits
```

3. テンプレートマッチで CSV 生成
```bash
uv run tools/generate_measurement_csv.py \
  --video resources/videos/2026-02-06\ 21-46-02.mp4 \
  --rois resources/rois/rois.json \
  --templates-dir resources/templates/game_digits \
  --out-csv resources/switch2/readings.csv \
  --out-diagnostics-csv resources/switch2/readings_diagnostics.csv \
  --log-every 1000 \
  --min-support 2
```

主要オプション:
- `--start-frame`: 処理開始の動画フレーム番号
- `--end-frame`: 処理終了の動画フレーム番号（`-1` で末尾まで）
- `--log-every`: 進捗ログ出力間隔（処理フレーム数、`0` で無効）
- `--min-support`: 同一 barcode frame 内で採用する最小 run 長

進捗ログには `accepted`（有効観測数）と、バーコード失敗内訳（`decode_fail` / `preamble_fail` / `crc_fail`）、直近成功サンプルが表示されます。

補足:
- 同期ずれ対策として、同一 barcode frame 内で最長継続した `(sx,sy,gx,gy)` を採用します。
- 可視化確認用は [tools/read_values.py](tools/read_values.py)（CSV生成責務なし）です。

可視化デバッグ例:
```bash
uv run tools/read_values.py \
  --video resources/videos/2026-02-06\ 21-46-02.mp4 \
  --rois resources/rois/rois.json \
  --templates-dir resources/templates/game_digits
```

操作キー:
- `space`: 再生/停止
- `.` `,`: 1フレーム送り/戻し
- `e` `f`: 10フレーム送り/戻し
- `p`: 現在の読取値を標準出力へ表示
- `q` or `Esc`: 終了

## ビルド（Pico 側）
このリポジトリには CMake ベースの Pico プロジェクト群（`examples/`）が含まれます。

前提（例）:
- `cmake`
- `ninja`（任意）
- `arm-none-eabi-gcc`

ビルド:
```bash
cmake -B build -S .
cmake --build build -j
```

ターゲット指定例:
```bash
cmake --build build --target measure
```

## フラッシュ
BOOTSEL でマスストレージに `.uf2` をコピーするか、`picotool` を使います。

```bash
cp build/examples/measure/measure.uf2 /Volumes/RPI-RP2/
```

## リポジトリの見方
- `examples/`: 実験単位の Pico 実装
- `tools/`: 動画処理・可視化・データ化スクリプト
- `resources/`: 動画、ROI、テンプレート等の素材
- `docs/`: 背景、構成、測定手順

## 注意事項
- Joybus はタイミング依存です。ISR で重い処理やブロッキング I/O は避けます。
- Python スクリプト実行は `uv` 前提です。
- 変更時は [docs/measurements.md](docs/measurements.md) の手順を壊さないことを優先します。

## ライセンス
依存物（例: pico-sdk, picotool）を含め、各コンポーネントはそれぞれのライセンスに従います。
