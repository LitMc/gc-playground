import argparse
from pathlib import Path

import cv2 as cv


def read_frame_at(cap: cv.VideoCapture, idx: int):
    cap.set(cv.CAP_PROP_POS_FRAMES, idx)
    ret, frame = cap.read()
    return ret, frame


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video", help="input video path")
    ap.add_argument("--start", type=int, default=0, help="start frame index")
    args = ap.parse_args()

    video_path = Path(args.video).resolve()
    cap = cv.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Failed to open video: {video_path}")
    fps = cap.get(cv.CAP_PROP_FPS) or 0.0
    frame_count = int(cap.get(cv.CAP_PROP_FRAME_COUNT) or 0)
    cur = max(0, args.start)
    paused = True

    ret, frame = read_frame_at(cap, cur)
    if not ret:
        raise RuntimeError(
            f"Failed to read frame at index {cur} from video: {video_path}"
        )

    win = "frame-player"
    cv.namedWindow(win, cv.WINDOW_NORMAL)

    def draw_overlay(img, text):
        vis = img.copy()
        cv.putText(
            vis, text, (10, 30), cv.FONT_HERSHEY_COMPLEX, 0.9, (0, 0, 0), 4, cv.LINE_AA
        )
        cv.putText(
            vis,
            text,
            (10, 30),
            cv.FONT_HERSHEY_COMPLEX,
            0.9,
            (255, 255, 255),
            2,
            cv.LINE_AA,
        )
        return vis

    while True:
        t = (cur / fps) if fps > 0 else 0.0
        info = f"frame={cur}"
        if fps > 0:
            info += f" time={t:.3f}s fps={fps:.2f}"
        if frame_count > 0:
            info += f" / {frame_count - 1}"
        if paused:
            info += " [PAUSED]"
        cv.imshow(win, draw_overlay(frame, info))

        delay = 0 if paused else max(1, int(1000 / (fps if fps > 0 else 30)))
        key = cv.waitKeyEx(delay)
        if key == -1 and not paused:
            cur += 1
            if frame_count > 0 and cur >= frame_count:
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
        else:
            if key in (2424832, 81):
                cur -= 1
            elif key in (2555904, 83):
                cur += 1
            else:
                continue

        if frame_count > 0:
            cur = max(0, min(cur, frame_count - 1))
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
