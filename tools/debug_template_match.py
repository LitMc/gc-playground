import argparse

import cv2 as cv
import numpy as np
from measurement_lib import (
    crop,
    decode_axis_value,
    decode_barcode,
    ensure_roi_names,
    load_rois,
    load_templates,
    parse_barcode_bits,
)
from measurement_lib.ocr_templates import (
    classify_digit_roi,
    classify_sign_roi,
)

REQUIRED_ROIS = [
    "barcode",
    "x_sign",
    "x_100",
    "x_10",
    "x_1",
    "y_sign",
    "y_100",
    "y_10",
    "y_1",
]


def read_frame_at(cap: cv.VideoCapture, idx: int):
    cap.set(cv.CAP_PROP_POS_FRAMES, idx)
    ret, frame = cap.read()
    return ret, frame


def draw_overlay(img, text, x=10, y=30, size=0.6):
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
        1,
        cv.LINE_AA,
    )
    return vis


def draw_rectangle(img, roi, color=(255, 255, 0), thickness=1):
    vis = img.copy()
    cv.rectangle(
        vis,
        (roi.x, roi.y),
        (roi.x + roi.w, roi.y + roi.h),
        color,
        thickness,
    )
    return vis


def inspect_axis(frame, rois, axis, bank):
    out = {}

    sign_key = f"{axis}_sign"
    sign_roi = crop(frame, rois[sign_key])
    sign_token_raw, minus_sign_score, blank_sign_score, plus_sign_score, ink_sign = (
        classify_sign_roi(sign_roi, bank)
    )
    if sign_token_raw == "-":
        sign_token = "-"
    elif sign_token_raw == "+":
        sign_token = "+"
    else:
        sign_token = "blank/+"
    out[sign_key] = {
        "token": sign_token,
        "digit_score": max(minus_sign_score, plus_sign_score),
        "minus_score": minus_sign_score,
        "blank_score": blank_sign_score,
        "plus_score": plus_sign_score,
        "ink_ratio": ink_sign,
    }

    for place in (100, 10, 1):
        key = f"{axis}_{place}"
        roi = crop(frame, rois[key])

        token_raw, digit_score, minus_digit_score, blank_score, ink_ratio = (
            classify_digit_roi(
                roi,
                bank,
                allow_blank=(place != 1),
            )
        )

        if token_raw is None:
            token = "blank"
        elif token_raw == "-":
            token = "-"
        else:
            token = str(token_raw)

        out[key] = {
            "token": token,
            "digit_score": digit_score,
            "minus_score": minus_digit_score,
            "blank_score": blank_score,
            "plus_score": -1.0,
            "ink_ratio": ink_ratio,
        }

    return out


def format_roi_line(key: str, values: dict):
    return (
        f"{key:>6} tok={values['token']:<7} "
        f"d={values['digit_score']:.2f} "
        f"m={values['minus_score']:.2f} "
        f"b={values.get('blank_score', -1.0):.2f} "
        f"p={values['plus_score']:.2f} "
        f"ink={values['ink_ratio']:.2f}"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True, help="Path to the video file")
    ap.add_argument("--rois", required=True, help="Path to the ROIs file")
    ap.add_argument("--templates-dir", required=True, help="Template directory path")
    ap.add_argument(
        "--frame", type=int, default=0, help="Frame number to start processing from"
    )
    args = ap.parse_args()

    cap = cv.VideoCapture(args.video)
    if not cap.isOpened():
        raise RuntimeError(f"Error opening video: {args.video}")

    rois = load_rois(args.rois)
    ensure_roi_names(rois, REQUIRED_ROIS)

    bank = load_templates(args.templates_dir)
    if bank is None:
        raise RuntimeError("Template loading failed")

    cur = max(0, args.frame)
    ret, frame = read_frame_at(cap, cur)
    if not ret:
        raise RuntimeError(f"Failed to read frame at index {cur}")

    fps = cap.get(cv.CAP_PROP_FPS)
    if fps <= 0:
        fps = 30.0
    frame_count = int(cap.get(cv.CAP_PROP_FRAME_COUNT))
    if frame_count <= 0:
        frame_count = -1

    paused = True
    win = "debug_template_match"
    cv.namedWindow(win, cv.WINDOW_NORMAL)

    while True:
        barcode_roi = crop(frame, rois["barcode"])
        _, bits, _ = decode_barcode(barcode_roi, 48)
        barcode_dec = parse_barcode_bits(bits)

        x_res = decode_axis_value(frame, rois, "x", bank)
        y_res = decode_axis_value(frame, rois, "y", bank)

        x_info = inspect_axis(frame, rois, "x", bank)
        y_info = inspect_axis(frame, rois, "y", bank)

        video_vis = frame.copy()
        video_vis = draw_rectangle(
            video_vis, rois["barcode"], color=(0, 255, 255), thickness=1
        )

        for name in (
            "x_sign",
            "x_100",
            "x_10",
            "x_1",
            "y_sign",
            "y_100",
            "y_10",
            "y_1",
        ):
            video_vis = draw_rectangle(
                video_vis, rois[name], color=(255, 255, 0), thickness=1
            )

        panel_width = 560
        panel = np.zeros((video_vis.shape[0], panel_width, 3), dtype=np.uint8)
        panel = draw_overlay(
            panel,
            "q/esc:quit  r/R or Enter:play/pause  .,:step  e/f:+-10  [/]:+-1000  g:jump  p:print",
            x=8,
            y=20,
            size=0.48,
        )
        panel = draw_overlay(panel, f"video_frame={cur}", x=8, y=42, size=0.55)

        if barcode_dec is None:
            panel = draw_overlay(panel, "barcode: decode failed", x=8, y=64, size=0.52)
        else:
            panel = draw_overlay(
                panel,
                f"raw frame={barcode_dec.frame} sx={barcode_dec.sx} sy={barcode_dec.sy} pre={int(barcode_dec.preamble_ok)} crc={int(barcode_dec.crc_ok)}",
                x=8,
                y=64,
                size=0.52,
            )

        panel = draw_overlay(
            panel,
            f"x={x_res.value} conf={x_res.confidence:.3f}  y={y_res.value} conf={y_res.confidence:.3f}",
            x=8,
            y=86,
            size=0.52,
        )

        ordered_keys = [
            "x_sign",
            "x_100",
            "x_10",
            "x_1",
            "y_sign",
            "y_100",
            "y_10",
            "y_1",
        ]

        merged_info = {**x_info, **y_info}
        panel_x = 8
        panel_y = 112
        line_h = 20
        panel = draw_overlay(panel, "ROI match details", x=panel_x, y=panel_y, size=0.5)
        for i, key in enumerate(ordered_keys):
            values = merged_info[key]
            line = format_roi_line(key, values)
            panel = draw_overlay(
                panel,
                line,
                x=panel_x,
                y=panel_y + (i + 1) * line_h,
                size=0.42,
            )

            roi = rois[key]
            token_text = str(values["token"])
            token_y = max(12, roi.y + roi.h - 2)
            video_vis = draw_overlay(
                video_vis,
                token_text,
                x=roi.x + 2,
                y=token_y,
                size=0.45,
            )

        composed = np.hstack([video_vis, panel])
        cv.imshow(win, composed)

        delay = 0 if paused else max(1, int(1000 / fps))
        key_raw = cv.waitKey(delay)

        if key_raw == -1 and not paused:
            cur += 1
            if frame_count > 0 and cur >= frame_count:
                cur = frame_count - 1
                paused = True
                continue
            ret, frame = cap.read()
            if not ret:
                paused = True
            continue

        key = key_raw & 0xFF

        if key in (ord("q"), 27):
            break
        if key in (ord("r"), ord("R"), 13):
            paused = not paused
            print(f"playback paused={paused}")
            continue
        if key in (ord("."), ord(">")):
            cur += 1
        elif key in (ord(","), ord("<")):
            cur -= 1
        elif key == ord("e"):
            cur += 10
        elif key == ord("f"):
            cur -= 10
        elif key == ord("]"):
            cur += 1000
        elif key == ord("["):
            cur -= 1000
        elif key == ord("g"):
            try:
                s = input("jump to video frame index> ").strip()
                if s:
                    cur = int(s)
            except ValueError:
                print("invalid frame index")
            paused = True
            if frame_count > 0:
                cur = max(0, min(frame_count - 1, cur))
            else:
                cur = max(0, cur)

            ret, jumped_frame = read_frame_at(cap, cur)
            if ret:
                frame = jumped_frame
            else:
                print(f"failed to jump frame: {cur}")
            continue
        elif key == ord("p"):
            print(f"video_frame={cur}")
            if barcode_dec is not None:
                print(
                    f"raw frame={barcode_dec.frame} sx={barcode_dec.sx} sy={barcode_dec.sy} pre={barcode_dec.preamble_ok} crc={barcode_dec.crc_ok}"
                )
            print(f"x={x_res.value} conf={x_res.confidence:.3f}")
            print(f"y={y_res.value} conf={y_res.confidence:.3f}")
            continue
        else:
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
