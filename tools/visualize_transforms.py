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
h1 { font-size: 18px; margin-bottom: 16px; color: #a0c4ff; }
canvas {
    border: 1px solid #444;
    cursor: crosshair;
}
.info {
    margin-top: 12px;
    font-size: 12px;
    color: #888;
}
</style>
</head>
<body>
<h1>S — Switch 2 実測変換</h1>
<canvas id="cv" width="512" height="512"></canvas>
<div class="info">
    <span style="color:#fff">●</span> s（入力）
    <span style="color:#ff6b6b">●</span> S(s)（出力）
    &emsp; 白線: Oct(100)
</div>

<script>
const S = __S_DATA__;
const N = 256;
const PX = 2;
const SZ = N * PX;

const cv = document.getElementById('cv');
const ctx = cv.getContext('2d');

// ── 正八角形 Oct(a) ──
// 頂点が 0°, 45°, 90°, ... の 8 方向に距離 a
// 辺の法線は 22.5° 間隔、apothem = a·cos(π/8)
const OCT_A = 100;
const C8 = Math.cos(Math.PI / 8);
const S8 = Math.sin(Math.PI / 8);
const OCT_H = OCT_A * C8;

function inOct(cx, cy) {
    return Math.abs(C8*cx + S8*cy) <= OCT_H
        && Math.abs(C8*cx - S8*cy) <= OCT_H
        && Math.abs(S8*cx + C8*cy) <= OCT_H
        && Math.abs(S8*cx - C8*cy) <= OCT_H;
}

const OCT_VERTS = __OCT_VERTS__;

// grid (0..255) → canvas pixel。Y 反転（0 が下）
function g2c(gx, gy) {
    return [gx * PX + PX / 2, (N - 1 - gy) * PX + PX / 2];
}

function dot(gx, gy, color, r) {
    const [x, y] = g2c(gx, gy);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
}

function label(gx, gy, text, color) {
    const [x, y] = g2c(gx, gy);
    ctx.font = '11px monospace';
    ctx.fillStyle = color;
    // ラベルの位置をキャンバス端に応じて調整
    const metrics = ctx.measureText(text);
    const tw = metrics.width;
    let lx = x + 8;
    let ly = y - 8;
    if (lx + tw > SZ) lx = x - tw - 8;
    if (ly < 12) ly = y + 16;
    ctx.fillText(text, lx, ly);
}

function drawBg() {
    // Oct₁₂₈ 領域を塗り分け
    const img = ctx.createImageData(SZ, SZ);
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
    ctx.putImageData(img, 0, 0);

    // 中心線
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    const [cx, cy] = g2c(128, 128);
    ctx.beginPath();
    ctx.moveTo(cx, 0); ctx.lineTo(cx, SZ);
    ctx.moveTo(0, cy); ctx.lineTo(SZ, cy);
    ctx.stroke();

    // Oct 境界
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

// ── インタラクション ──
let curSx = -1, curSy = -1;

function update() {
    drawBg();
    if (curSx < 0) return;

    const sx = curSx, sy = curSy;
    const [mx, my] = S[sx][sy];

    // s → S(s) の線
    const [x1, y1] = g2c(sx, sy);
    const [x2, y2] = g2c(mx, my);
    ctx.strokeStyle = 'rgba(255,255,255,0.2)';
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 3]);
    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.stroke();
    ctx.setLineDash([]);

    // 点とラベル
    dot(sx, sy, '#ffffff', 4);
    label(sx, sy, `s (${sx}, ${sy})`, '#ffffff');

    dot(mx, my, '#ff6b6b', 5);
    label(mx, my, `S(s) (${mx}, ${my})`, '#ff6b6b');
}

cv.addEventListener('mousemove', (e) => {
    const r = cv.getBoundingClientRect();
    const sx = Math.floor((e.clientX - r.left) / PX);
    const sy = N - 1 - Math.floor((e.clientY - r.top) / PX);
    if (sx >= 0 && sx < N && sy >= 0 && sy < N) {
        curSx = sx; curSy = sy;
        update();
    }
});

cv.addEventListener('mouseleave', () => {
    curSx = -1; curSy = -1;
    drawBg();
});

drawBg();
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
