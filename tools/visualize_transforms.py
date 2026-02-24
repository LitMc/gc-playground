"""変換ビューア: Switch 2 の変換 S をインタラクティブに可視化する HTML を生成する。

readings.csv を読み込み、入力 s = (sx, sy) から出力 m = S(s) への対応を
マウスホバーで確認できるスタンドアロン HTML を出力する。

記号の定義:
    s = (sx, sy) ∈ [0, 255]²  — コントローラの生入力
    m = S(s) = (gx+128, gy+128) — Switch 2 変換後のゲーム受信値
    Oct(a) — 正八角形（GC コントローラのゲートと同じ向き）
             頂点が 0°, 45°, 90°, ... の 8 方向（縦横・斜め）
             縦横の到達距離 a, 斜め45°の軸成分 a/√2

使い方:
    uv run tools/visualize_transforms.py \\
        --input resources/switch2/20260205/readings.csv \\
        --output resources/switch2/20260205/transform_viewer.html
"""

import argparse
import csv
import json
import math
from pathlib import Path


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Switch 2 変換 S をインタラクティブに可視化する HTML を生成する"
    )
    ap.add_argument("--input", required=True, help="readings.csv のパス")
    ap.add_argument("--output", required=True, help="出力 HTML ファイルのパス")
    return ap.parse_args()


def read_s_data(path: Path) -> list[list[list[int]]]:
    """readings.csv → S[sx][sy] = [mx, my] の 256×256 配列。

    S(sx, sy) = (gx + 128, gy + 128)
    """
    grid: list[list[list[int]]] = [[[128, 128] for _ in range(256)] for _ in range(256)]
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            sx, sy = int(row["sx"]), int(row["sy"])
            gx, gy = int(row["gx"]), int(row["gy"])
            grid[sx][sy] = [gx + 128, gy + 128]
    return grid


def oct_vertices_centered(a: float) -> list[list[float]]:
    """正八角形 Oct(a) の 8 頂点（centered 座標）。

    頂点は 0°, 45°, 90°, ... の 8 方向に距離 a。
    水平・垂直な辺はない。
    """
    d = a / math.sqrt(2)
    return [
        [a, 0], [d, d], [0, a], [-d, d],
        [-a, 0], [-d, -d], [0, -a], [d, -d],
    ]


HTML_TEMPLATE = """\
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="utf-8">
<title>変換ビューア — S</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: monospace;
    background: #1a1a2e;
    color: #e0e0e0;
    display: flex;
    flex-direction: column;
    align-items: center;
    min-height: 100vh;
    padding: 16px;
}
h1 { font-size: 18px; margin-bottom: 12px; color: #a0c4ff; }
.canvases {
    display: flex;
    gap: 24px;
    flex-wrap: wrap;
    justify-content: center;
}
.canvas-wrapper {
    display: flex;
    flex-direction: column;
    align-items: center;
}
.canvas-wrapper h2 {
    font-size: 14px;
    margin-bottom: 4px;
    color: #ccc;
}
canvas {
    border: 1px solid #444;
    cursor: crosshair;
}
.info {
    margin-top: 16px;
    font-size: 14px;
    line-height: 2.0;
    background: #16213e;
    padding: 12px 20px;
    border-radius: 6px;
    min-width: 460px;
}
.s-val { color: #ff6b6b; }
.dim { color: #666; }
.legend {
    margin-top: 8px;
    font-size: 12px;
    color: #888;
}
</style>
</head>
<body>
<h1>S — Switch 2 実測変換</h1>

<div class="canvases">
    <div class="canvas-wrapper">
        <h2>入力 s = (sx, sy)</h2>
        <canvas id="cvIn" width="512" height="512"></canvas>
    </div>
    <div class="canvas-wrapper">
        <h2>出力 m = S(s)</h2>
        <canvas id="cvOut" width="512" height="512"></canvas>
    </div>
</div>

<div class="info" id="info">マウスを左キャンバス上に移動してください</div>
<div class="legend">
    <span style="color:#ff6b6b">●</span> S(s)
    &emsp; 白線: Oct(100)（頂点が縦横・斜めを向く正八角形）
</div>

<script>
const S = __S_DATA__;
const N = 256;
const PX = 2;
const SZ = N * PX;

const cvIn  = document.getElementById('cvIn');
const cvOut = document.getElementById('cvOut');
const ctxIn  = cvIn.getContext('2d');
const ctxOut = cvOut.getContext('2d');
const info = document.getElementById('info');

// ── 正八角形 Oct(a) ──
// 頂点が 0°, 45°, 90°, ... の 8 方向に距離 a
// 辺は 22.5°, 67.5°, ... の方向（水平・垂直な辺はない）
// 辺の法線は 22.5° 間隔、中心から辺までの距離（apothem） = a·cos(π/8)
//
// 判定: 4 つの制約（各 ±で 8 辺をカバー）
//   |cx·cos22.5° + cy·sin22.5°| ≤ h
//   |cx·cos22.5° − cy·sin22.5°| ≤ h
//   |cx·sin22.5° + cy·cos22.5°| ≤ h
//   |cx·sin22.5° − cy·cos22.5°| ≤ h
// ここで h = a·cos(π/8) = a·cos22.5°
const OCT_A = 100;
const C8 = Math.cos(Math.PI / 8);  // cos(22.5°) ≈ 0.9239
const S8 = Math.sin(Math.PI / 8);  // sin(22.5°) ≈ 0.3827
const OCT_H = OCT_A * C8;          // apothem ≈ 92.39

function inOct(cx, cy) {
    return Math.abs(C8*cx + S8*cy) <= OCT_H
        && Math.abs(C8*cx - S8*cy) <= OCT_H
        && Math.abs(S8*cx + C8*cy) <= OCT_H
        && Math.abs(S8*cx - C8*cy) <= OCT_H;
}

// Oct(a) の 8 頂点 (centered): 0°, 45°, 90°, ... に距離 a
const OCT_VERTS = __OCT_VERTS__;

// grid → canvas pixel
function g2c(gx, gy) {
    return [gx * PX + PX / 2, (N - 1 - gy) * PX + PX / 2];
}

function dot(ctx, gx, gy, color, r) {
    const [x, y] = g2c(gx, gy);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
}

function cross(ctx, gx, gy, color) {
    const [x, y] = g2c(gx, gy);
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(x, 0); ctx.lineTo(x, SZ);
    ctx.moveTo(0, y); ctx.lineTo(SZ, y);
    ctx.stroke();
    ctx.setLineDash([]);
}

function drawOctBoundary(ctx) {
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    const [x0, y0] = g2c(128 + OCT_VERTS[0][0], 128 + OCT_VERTS[0][1]);
    ctx.moveTo(x0, y0);
    for (let i = 1; i < OCT_VERTS.length; i++) {
        const [x, y] = g2c(128 + OCT_VERTS[i][0], 128 + OCT_VERTS[i][1]);
        ctx.lineTo(x, y);
    }
    ctx.closePath();
    ctx.stroke();
}

// ── 背景描画 ──
function drawInBg() {
    ctxIn.fillStyle = '#1e1e3a';
    ctxIn.fillRect(0, 0, SZ, SZ);
    // 中心線
    ctxIn.strokeStyle = 'rgba(255,255,255,0.1)';
    ctxIn.lineWidth = 1;
    const [cx, cy] = g2c(128, 128);
    ctxIn.beginPath();
    ctxIn.moveTo(cx, 0); ctxIn.lineTo(cx, SZ);
    ctxIn.moveTo(0, cy); ctxIn.lineTo(SZ, cy);
    ctxIn.stroke();
}

function drawOutBg() {
    const img = ctxOut.createImageData(SZ, SZ);
    for (let mx = 0; mx < N; mx++)
        for (let my = 0; my < N; my++) {
            const inside = inOct(mx - 128, my - 128);
            const r = inside ? 28 : 18;
            const g = inside ? 33 : 18;
            const b = inside ? 52 : 28;
            for (let px = 0; px < PX; px++)
                for (let py = 0; py < PX; py++) {
                    const i = ((N-1-my)*PX+py) * SZ + mx*PX+px;
                    img.data[i*4]=r; img.data[i*4+1]=g;
                    img.data[i*4+2]=b; img.data[i*4+3]=255;
                }
        }
    ctxOut.putImageData(img, 0, 0);
    drawOctBoundary(ctxOut);
}

// ── インタラクション ──
let curSx = -1, curSy = -1;

function update() {
    if (curSx < 0) return;
    const sx = curSx, sy = curSy;
    const [mx, my] = S[sx][sy];

    drawInBg();
    cross(ctxIn, sx, sy, 'rgba(255,255,255,0.4)');
    dot(ctxIn, sx, sy, '#fff', 4);

    drawOutBg();
    cross(ctxOut, mx, my, 'rgba(255,107,107,0.3)');
    dot(ctxOut, mx, my, '#ff6b6b', 5);

    info.innerHTML =
        `s = (${sx}, ${sy})` +
        `<span class="dim"> = center + (${sx-128}, ${sy-128})</span>` +
        `<br>S(s) = <span class="s-val">(${mx}, ${my})</span>` +
        `<span class="dim"> = center + (${mx-128}, ${my-128})</span>`;
}

cvIn.addEventListener('mousemove', (e) => {
    const r = cvIn.getBoundingClientRect();
    const sx = Math.floor((e.clientX - r.left) / PX);
    const sy = N - 1 - Math.floor((e.clientY - r.top) / PX);
    if (sx >= 0 && sx < N && sy >= 0 && sy < N) {
        curSx = sx; curSy = sy;
        update();
    }
});

cvIn.addEventListener('mouseleave', () => {
    curSx = -1; curSy = -1;
    drawInBg(); drawOutBg();
    info.textContent = 'マウスを左キャンバス上に移動してください';
});

drawInBg();
drawOutBg();
</script>
</body>
</html>
"""


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)

    print(f"読み込み: {input_path}")
    s_data = read_s_data(input_path)

    s_json = json.dumps(s_data, separators=(",", ":"))
    oct_verts_json = json.dumps(oct_vertices_centered(100))

    html = HTML_TEMPLATE.replace("__S_DATA__", s_json)
    html = html.replace("__OCT_VERTS__", oct_verts_json)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(html, encoding="utf-8")
    print(f"出力: {output_path}")
    print(f"ブラウザで開いてください: file://{output_path.resolve()}")


if __name__ == "__main__":
    main()
