import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import List, Optional, Tuple

import cv2 as cv


@dataclass
class Roi:
    name: str
    x: int
    y: int
    w: int
    h: int


def clamp_rect(x0, y0, x1, y1, W, H):
    x0 = max(0, min(W - 1, x0))
    y0 = max(0, min(H - 1, y0))
    x1 = max(0, min(W - 1, x1))
    y1 = max(0, min(H - 1, y1))
    x_min, x_max = sorted([x0, x1])
    y_min, y_max = sorted([y0, y1])
    return x_min, y_min, x_max, y_max


def dist_point_to_rect(px, py, r: Roi) -> float:
    dx = 0
    if px < r.x:
        dx = r.x - px
    elif px > r.x + r.w:
        dx = px - (r.x + r.w)
    dy = 0
    if py < r.y:
        dy = r.y - py
    elif py > r.y + r.h:
        dy = py - (r.y + r.h)
    return math.hypot(dx, dy)


class RoiPicker:
    def __init__(self, frame, names: List[str], start_name_idx: int = 0):
        self.frame = frame
        self.H, self.W = frame.shape[:2]
        self.scale = 1.0

        self.names = names
        self.name_idx = start_name_idx

        self.rois: List[Roi] = []
        self.dragging = False
        self.p0: Optional[Tuple[int, int]] = None
        self.p1: Optional[Tuple[int, int]] = None

        self.window = "roi-picker"
        cv.namedWindow(self.window, cv.WINDOW_NORMAL)
        cv.setMouseCallback(self.window, self.on_mouse)

    @property
    def cur_name(self) -> str:
        return self.names[self.name_idx]

    def img_to_view(self, x, y):
        return int(x * self.scale), int(y * self.scale)

    def view_to_img(self, x, y):
        return int(x / self.scale), int(y / self.scale)

    def on_mouse(self, event, x, y, flags, userdata=None):
        ix, iy = self.view_to_img(x, y)

        if event == cv.EVENT_LBUTTONDOWN:
            self.dragging = True
            self.p0 = (ix, iy)
            self.p1 = (ix, iy)

        elif event == cv.EVENT_MOUSEMOVE and self.dragging:
            self.p1 = (ix, iy)

        elif event == cv.EVENT_LBUTTONUP and self.dragging:
            self.dragging = False
            self.p1 = (ix, iy)
            if self.p0 is None or self.p1 is None:
                return
            x0, y0 = self.p0
            x1, y1 = self.p1
            x_min, y_min, x_max, y_max = clamp_rect(x0, y0, x1, y1, self.W, self.H)
            w = max(1, x_max - x_min)
            h = max(1, y_max - y_min)
            self.rois.append(Roi(self.cur_name, x_min, y_min, w, h))
            self.p0 = None
            self.p1 = None

        elif event == cv.EVENT_RBUTTONDOWN:
            if not self.rois:
                return
            best_i = None
            best_d = 1e9
            for i, r in enumerate(self.rois):
                d = dist_point_to_rect(ix, iy, r)
                if d < best_d:
                    best_d = d
                    best_i = i
                if best_i is not None and best_d <= 20:
                    self.rois.pop(best_i)

    def draw(self):
        vis = self.frame.copy()

        for r in self.rois:
            cv.rectangle(vis, (r.x, r.y), (r.x + r.w, r.y + r.h), (0, 0, 255), 2)
            cv.putText(
                vis,
                r.name,
                (r.x, max(0, r.y - 6)),
                cv.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 0, 255),
                2,
                cv.LINE_AA,
            )

        if self.dragging and self.p0 and self.p1:
            x0, y0 = self.p0
            x1, y1 = self.p1
            x_min, y_min, x_max, y_max = clamp_rect(x0, y0, x1, y1, self.W, self.H)
            cv.rectangle(vis, (x_min, y_min), (x_max, y_max), (0, 255, 255), 2)

        hud = (
            f"name=[{self.cur_name}] ROIs={len(self.rois)} "
            f"zoom={self.scale:.2f} "
            "keys: [c]cycle name, [+/-]zoom [u] undo [x] clear [s]save [q]quit"
        )
        cv.putText(
            vis,
            hud,
            (10, 30),
            cv.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 0, 0),
            4,
            cv.LINE_AA,
        )
        cv.putText(
            vis,
            hud,
            (10, 30),
            cv.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 255),
            2,
            cv.LINE_AA,
        )

        if self.scale != 1.0:
            vis = cv.resize(
                vis,
                (int(self.W * self.scale), int(self.H * self.scale)),
                interpolation=cv.INTER_NEAREST,
            )

        cv.imshow(self.window, vis)

    def run(self):
        while True:
            self.draw()
            key = cv.waitKey(16) & 0xFF

            if key == ord("q"):
                break
            elif key == ord("c"):
                self.name_idx = (self.name_idx + 1) % len(self.names)
            elif key in (ord("+"), ord("=")):
                self.scale = min(6.0, self.scale * 1.25)
            elif key in (ord("-"), ord("_")):
                self.scale = max(0.25, self.scale / 1.25)
            elif key == ord("u"):
                if self.rois:
                    self.rois.pop()
            elif key == ord("x"):
                self.rois.clear()

        cv.destroyAllWindows()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True, help="Path to the video file")
    ap.add_argument("--frame", type=int, default=0, help="Frame index to capture")
    ap.add_argument("--out", default="rois.json")
    ap.add_argument(
        "--names",
        default="barcode,x_text,y_text",
        help="comma-separated roi names to cycle",
    )
    args = ap.parse_args()

    cap = cv.VideoCapture(args.video)
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video: {args.video}")

    cap.set(cv.CAP_PROP_POS_FRAMES, args.frame)
    ret, frame = cap.read()
    cap.release()

    if not ret:
        raise RuntimeError(f"Cannot read frame {args.frame} from video: {args.video}")

    names = [s.strip() for s in args.names.split(",") if s.strip()]
    picker = RoiPicker(frame, names)
    picker.run()

    out_path = Path(args.out).resolve()
    payload = {
        "video": str(Path(args.video).resolve()),
        "frame_index": args.frame,
        "frame_size": {"w": frame.shape[1], "h": frame.shape[0]},
        "rois": [asdict(r) for r in picker.rois],
    }
    out_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print("saved:", out_path)


if __name__ == "__main__":
    main()
