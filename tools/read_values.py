import argparse
import json
from dataclasses import dataclass
from pathlib import Path

import cv2 as cv


@dataclass(frozen=True)
class Roi:
    x: int
    y: int
    w: int
    h: int


def load_rois(path: str) -> dict[str, Roi]:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    rois = {}
    for r in data["rois"]:
        rois[r["name"]] = Roi(x=r["x"], y=r["y"], w=r["w"], h=r["h"])
    return rois


def crop(frame, roi: Roi):
    return frame[roi.y : roi.y + roi.h, roi.x : roi.x + roi.w]


def binarize_bgr(img_bgr):
    gray = cv.cvtColor(img_bgr, cv.COLOR_BGR2GRAY)
    _, th = cv.threshold(gray, 0, 255, cv.THRESH_BINARY | cv.THRESH_OTSU)
    return th


def read_frame_at(cap: cv.VideoCapture, idx: int):
    cap.set(cv.CAP_PROP_POS_FRAMES, idx)
    ret, frame = cap.read()
    return ret, frame


def draw_overlay(img, text, x=10, y=30, size=0.7):
    vis = img.copy()
    cv.putText(
        vis, text, (x, y), cv.FONT_HERSHEY_SIMPLEX, size, (0, 0, 0), 4, cv.LINE_AA
    )
    cv.putText(
        vis,
        text,
        (x, y),
        cv.FONT_HERSHEY_SIMPLEX,
        size,
        (255, 255, 255),
        2,
        cv.LINE_AA,
    )
    return vis


def draw_rectangle(img, roi: Roi, color=(0, 255, 0), thickness=2):
    vis = img.copy()
    cv.rectangle(
        vis,
        (roi.x, roi.y),
        (roi.x + roi.w, roi.y + roi.h),
        color,
        thickness,
    )
    return vis


def draw_barcode_scanline(img, roi: Roi, n_bits: int):
    vis = img.copy()
    W = img.shape[1]
    bar_w = roi.w / n_bits + 1
    for i in range(n_bits):
        x0 = int(i * roi.w / n_bits) + roi.x + int(bar_w / 2)
        x1 = x0 + 1
        x0 = max(0, min(W - 1, x0))
        x1 = max(0, min(W - 1, x1))
        color = (0, 255, 0)
        cv.rectangle(vis, (x0, roi.y), (x1, roi.y + roi.h), color, 1)
    return vis


def barcode_windows(W: int, n_bits: int, left=0.45, right=0.55):
    """各ビットの読み取り窓[x0, x1)を返す。x1はスライス終端なのでWまで"""
    wins = []
    for i in range(n_bits):
        x0 = int((i + left) * W / n_bits)
        x1 = int((i + right) * W / n_bits)

        x0 = max(0, min(W - 1, x0))
        x1 = max(0, min(W, x1))

        if x1 <= x0:  # 窓が潰れたら最低1px
            x1 = min(W, x0 + 1)

        wins.append((x0, x1))
    return wins


def decode_barcode(barcode_bgr, n_bits: int):
    gray = cv.cvtColor(barcode_bgr, cv.COLOR_BGR2GRAY)
    sig = gray.mean(axis=0)
    thr = (float(sig.min()) + float(sig.max())) / 2.0

    W = sig.shape[0]
    wins = barcode_windows(W, n_bits)
    bits = []
    for x0, x1 in wins:
        v = sig[x0:x1].mean()
        is_black = v < thr
        bit = 0 if is_black else 1
        bits.append(bit)

    value = 0
    for b in bits:
        value = (value << 1) | b
    return value, bits, wins


def draw_barcode_windows(img, roi: Roi, wins, thickness=1):
    vis = img.copy()
    y0, y1 = roi.y, roi.y + roi.h - 1
    for i, (x0, x1) in enumerate(wins):
        fx0 = roi.x + x0
        fx1 = roi.x + x1 - 1
        cv.rectangle(vis, (fx0, y0), (fx1, y1), (0, 255, 0), thickness)

        cx = (fx0 + fx1) // 2
        cy = (y0 + y1) // 2
        cv.circle(vis, (cx, cy), 2, (0, 0, 255), -1)
    return vis


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True, help="Path to the video file")
    ap.add_argument("--rois", required=True, help="Path to the ROIs file")
    ap.add_argument(
        "--frame", type=int, default=0, help="Frame number to start processing from"
    )
    args = ap.parse_args()
    start_frame = args.frame

    cap = cv.VideoCapture(args.video)
    if not cap.isOpened():
        raise RuntimeError(f"Error opening video: {args.video}")

    rois = load_rois(args.rois)
    print(f"Loaded ROIs: {list(rois.keys())}")

    cap.set(cv.CAP_PROP_POS_FRAMES, args.frame)
    cur = start_frame
    ret, frame = read_frame_at(cap, cur)
    if not ret:
        raise RuntimeError(f"Failed to read frame at index {cur}")

    win = "video"
    cv.namedWindow(win, cv.WINDOW_NORMAL)

    fps = cap.get(cv.CAP_PROP_FPS)
    if fps <= 0:
        fps = 30.0
    frame_count = int(cap.get(cv.CAP_PROP_FRAME_COUNT))
    if frame_count <= 0:
        frame_count = -1
    paused = True
    n_bits = 48
    while True:
        barcode_roi = crop(frame, rois["barcode"])
        value, bits, wins = decode_barcode(barcode_roi, n_bits)

        vis = draw_overlay(
            frame, f"bits: {''.join(map(str, bits))}", x=0, y=200, size=0.6
        )
        vis = draw_overlay(vis, f"value: {value}", x=0, y=220, size=0.6)
        vis = draw_rectangle(vis, rois["barcode"], color=(0, 255, 255), thickness=1)
        vis = draw_barcode_windows(vis, rois["barcode"], wins)
        cv.imshow(win, vis)
        delay = 0 if paused else max(1, int(1000 / fps))
        key = cv.waitKey(delay) & 0xFF
        if key == -1 and not paused:
            cur += 1
            if cur >= frame_count:
                paused = True
                cur = frame_count - 1
                continue
            ret, frame = cap.read()
            if not ret:
                paused = True
            continue

        k = key & 0xFF
        if k == ord("q"):
            break
        if k in (ord("."), ord(">")):
            cur += 1
        elif k in (ord(","), ord("<")):
            cur -= 1
        elif k == ord("e"):
            cur += 10
        elif k == ord("f"):
            cur -= 10
        elif k == ord("p"):
            print(f"Current frame: {cur}")
            continue

        if frame_count > 0:
            cur = max(0, min(frame_count - 1, cur))
        else:
            cur = max(0, cur)
        ret, new_frame = read_frame_at(cap, cur)
        if ret:
            frame = new_frame
        else:
            paused = True

    cap.release()
    cv.destroyAllWindows()


if __name__ == "__main__":
    main()
