"""変換ビューア: S および φ をインタラクティブに可視化する HTML を生成する。

readings.csv を読み込み、各変換の入出力対応を
マウスホバーで確認できるスタンドアロン HTML を出力する。

記号の定義:
    s = (sx, sy) ∈ [0, 255]²  — コントローラの生入力
    m = S(s) = (gx+128, gy+128) — Switch 2 変換後のゲーム受信値
    φ(s) = k·(s − 128) + 128    — 物理→ソフトウェア八角形の線形スケーリング
    k = 100/125 = 4/5           — Oct(125) → Oct(100)
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
        description="S および φ をインタラクティブに可視化する HTML を生成する"
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
<title>変換ビューア — S / φ</title>
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
.tabs {
    display: flex;
    gap: 4px;
    margin-bottom: 12px;
}
.tabs button {
    font-family: monospace;
    font-size: 14px;
    padding: 6px 20px;
    border: 1px solid #555;
    background: #2a2a3e;
    color: #888;
    cursor: pointer;
    border-radius: 4px 4px 0 0;
}
.tabs button.active {
    background: #3a3a5e;
    color: #a0c4ff;
    border-bottom-color: #3a3a5e;
}
.tab-panel { display: none; flex-direction: column; align-items: center; }
.tab-panel.active { display: flex; }
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
<h1>変換ビューア</h1>
<div class="tabs">
    <button id="tabS" class="active" onclick="switchTab('S')">S</button>
    <button id="tabPhi" onclick="switchTab('phi')">φ</button>
</div>

<div id="panelS" class="tab-panel active">
    <canvas id="cvS" width="512" height="512"></canvas>
    <div class="info">
        <span style="color:#fff">●</span> s（入力）
        <span style="color:#ff6b6b">●</span> S(s)（出力）
        &emsp; 白線: Oct(100)
    </div>
</div>

<div id="panelPhi" class="tab-panel">
    <canvas id="cvPhi" width="512" height="512"></canvas>
    <div class="info">
        <span style="color:#fff">●</span> s（入力）
        <span style="color:#6bcaff">●</span> φ(s)（出力）
        &emsp; 白線: Oct(100)　<span style="color:#ffd700">━</span> Oct(125)
    </div>
</div>

<script>
const S_DATA = __S_DATA__;
const N = 256;
const PX = 2;
const SZ = N * PX;

const C8 = Math.cos(Math.PI / 8);
const S8 = Math.sin(Math.PI / 8);

const OCT100_VERTS = __OCT100_VERTS__;
const OCT125_VERTS = __OCT125_VERTS__;

function inOctA(cx, cy, a) {
    const h = a * C8;
    return Math.abs(C8*cx + S8*cy) <= h
        && Math.abs(C8*cx - S8*cy) <= h
        && Math.abs(S8*cx + C8*cy) <= h
        && Math.abs(S8*cx - C8*cy) <= h;
}

function g2c(gx, gy) {
    return [gx * PX + PX / 2, (N - 1 - gy) * PX + PX / 2];
}

function dot(c, gx, gy, color, r) {
    const [x, y] = g2c(gx, gy);
    c.fillStyle = color;
    c.beginPath();
    c.arc(x, y, r, 0, Math.PI * 2);
    c.fill();
}

function label(c, gx, gy, text, color) {
    const [x, y] = g2c(gx, gy);
    c.font = '11px monospace';
    c.fillStyle = color;
    const tw = c.measureText(text).width;
    let lx = x + 8, ly = y - 8;
    if (lx + tw > SZ) lx = x - tw - 8;
    if (ly < 12) ly = y + 16;
    c.fillText(text, lx, ly);
}

function drawOctOutline(c, verts, style, width) {
    c.strokeStyle = style;
    c.lineWidth = width;
    c.beginPath();
    const [x0, y0] = g2c(128 + verts[0][0], 128 + verts[0][1]);
    c.moveTo(x0, y0);
    for (let i = 1; i < verts.length; i++) {
        const [x, y] = g2c(128 + verts[i][0], 128 + verts[i][1]);
        c.lineTo(x, y);
    }
    c.closePath();
    c.stroke();
}

function drawCenterLines(c) {
    c.strokeStyle = 'rgba(255,255,255,0.08)';
    c.lineWidth = 1;
    const [cx, cy] = g2c(128, 128);
    c.beginPath();
    c.moveTo(cx, 0); c.lineTo(cx, SZ);
    c.moveTo(0, cy); c.lineTo(SZ, cy);
    c.stroke();
}

// ══════════════════════════════════════════
// S タブ
// ══════════════════════════════════════════
const cvS = document.getElementById('cvS');
const ctxS = cvS.getContext('2d');

let bgS = null;

function initBgS() {
    const img = ctxS.createImageData(SZ, SZ);
    for (let mx = 0; mx < N; mx++)
        for (let my = 0; my < N; my++) {
            const inside = inOctA(mx - 128, my - 128, 100);
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
    bgS = img;
}

function drawBgS() {
    ctxS.putImageData(bgS, 0, 0);
    drawCenterLines(ctxS);
    drawOctOutline(ctxS, OCT100_VERTS, 'rgba(255,255,255,0.35)', 1.5);
}

let curS = [-1, -1];

function updateS() {
    drawBgS();
    if (curS[0] < 0) return;
    const [sx, sy] = curS;
    const [mx, my] = S_DATA[sx][sy];

    const [x1, y1] = g2c(sx, sy);
    const [x2, y2] = g2c(mx, my);
    ctxS.strokeStyle = 'rgba(255,255,255,0.2)';
    ctxS.lineWidth = 1;
    ctxS.setLineDash([3, 3]);
    ctxS.beginPath();
    ctxS.moveTo(x1, y1); ctxS.lineTo(x2, y2);
    ctxS.stroke();
    ctxS.setLineDash([]);

    dot(ctxS, sx, sy, '#ffffff', 4);
    label(ctxS, sx, sy, `s (${sx}, ${sy})`, '#ffffff');
    dot(ctxS, mx, my, '#ff6b6b', 5);
    label(ctxS, mx, my, `S(s) (${mx}, ${my})`, '#ff6b6b');
}

cvS.addEventListener('mousemove', (e) => {
    const r = cvS.getBoundingClientRect();
    const sx = Math.floor((e.clientX - r.left) / PX);
    const sy = N - 1 - Math.floor((e.clientY - r.top) / PX);
    if (sx >= 0 && sx < N && sy >= 0 && sy < N) {
        curS = [sx, sy];
        updateS();
    }
});
cvS.addEventListener('mouseleave', () => { curS = [-1, -1]; drawBgS(); });

// ══════════════════════════════════════════
// φ タブ
// ══════════════════════════════════════════
const cvPhi = document.getElementById('cvPhi');
const ctxPhi = cvPhi.getContext('2d');

const K = 100 / 125;  // = 0.8

let bgPhi = null;

function initBgPhi() {
    const img = ctxPhi.createImageData(SZ, SZ);
    for (let mx = 0; mx < N; mx++)
        for (let my = 0; my < N; my++) {
            const cx = mx - 128, cy = my - 128;
            const in100 = inOctA(cx, cy, 100);
            const in125 = inOctA(cx, cy, 125);
            let r, g, b;
            if (in100)      { r = 28; g = 33; b = 52; }
            else if (in125) { r = 38; g = 30; b = 22; }
            else            { r = 18; g = 18; b = 28; }
            for (let px = 0; px < PX; px++)
                for (let py = 0; py < PX; py++) {
                    const i = ((N-1-my)*PX+py) * SZ + mx*PX+px;
                    img.data[i*4]=r; img.data[i*4+1]=g;
                    img.data[i*4+2]=b; img.data[i*4+3]=255;
                }
        }
    bgPhi = img;
}

function drawBgPhi() {
    ctxPhi.putImageData(bgPhi, 0, 0);
    drawCenterLines(ctxPhi);
    drawOctOutline(ctxPhi, OCT100_VERTS, 'rgba(255,255,255,0.35)', 1.5);
    drawOctOutline(ctxPhi, OCT125_VERTS, 'rgba(255,215,0,0.4)', 1.5);
}

function phi(sx, sy) {
    return [K * (sx - 128) + 128, K * (sy - 128) + 128];
}

let curPhi = [-1, -1];

function updatePhi() {
    drawBgPhi();
    if (curPhi[0] < 0) return;
    const [sx, sy] = curPhi;
    const [px, py] = phi(sx, sy);

    const [x1, y1] = g2c(sx, sy);
    const [x2, y2] = g2c(px, py);
    ctxPhi.strokeStyle = 'rgba(255,255,255,0.2)';
    ctxPhi.lineWidth = 1;
    ctxPhi.setLineDash([3, 3]);
    ctxPhi.beginPath();
    ctxPhi.moveTo(x1, y1); ctxPhi.lineTo(x2, y2);
    ctxPhi.stroke();
    ctxPhi.setLineDash([]);

    dot(ctxPhi, sx, sy, '#ffffff', 4);
    label(ctxPhi, sx, sy, `s (${sx}, ${sy})`, '#ffffff');
    dot(ctxPhi, px, py, '#6bcaff', 5);
    label(ctxPhi, px, py, `\\u03c6(s) (${px.toFixed(1)}, ${py.toFixed(1)})`, '#6bcaff');
}

cvPhi.addEventListener('mousemove', (e) => {
    const r = cvPhi.getBoundingClientRect();
    const sx = Math.floor((e.clientX - r.left) / PX);
    const sy = N - 1 - Math.floor((e.clientY - r.top) / PX);
    if (sx >= 0 && sx < N && sy >= 0 && sy < N) {
        curPhi = [sx, sy];
        updatePhi();
    }
});
cvPhi.addEventListener('mouseleave', () => { curPhi = [-1, -1]; drawBgPhi(); });

// ══════════════════════════════════════════
// タブ切り替え
// ══════════════════════════════════════════
function switchTab(name) {
    document.getElementById('tabS').classList.toggle('active', name === 'S');
    document.getElementById('tabPhi').classList.toggle('active', name === 'phi');
    document.getElementById('panelS').classList.toggle('active', name === 'S');
    document.getElementById('panelPhi').classList.toggle('active', name === 'phi');
    if (name === 'S') drawBgS();
    else drawBgPhi();
}

// 初期化
initBgS();
initBgPhi();
drawBgS();
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
    oct100_json = json.dumps(oct_vertices_centered(100))
    oct125_json = json.dumps(oct_vertices_centered(125))

    html = HTML_TEMPLATE.replace("__S_DATA__", s_json)
    html = html.replace("__OCT100_VERTS__", oct100_json)
    html = html.replace("__OCT125_VERTS__", oct125_json)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(html, encoding="utf-8")
    print(f"出力: {output_path}")
    print(f"ブラウザで開いてください: file://{output_path.resolve()}")


if __name__ == "__main__":
    main()
