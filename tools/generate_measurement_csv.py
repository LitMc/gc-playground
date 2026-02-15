import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import cv2 as cv
from measurement_lib import (
    ObservationRow,
    aggregate_longest_run,
    crop,
    decode_axis_value,
    decode_barcode,
    ensure_roi_names,
    load_rois,
    load_templates,
    parse_barcode_bits,
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


@dataclass(frozen=True)
class DiagnosticRow:
    frame: int
    support_len: int
    confidence_sum: float


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True, help="Path to the video file")
    ap.add_argument("--rois", required=True, help="Path to the ROIs file")
    ap.add_argument(
        "--templates-dir",
        required=True,
        help="Template dir containing digit_0*.png..digit_9*.png and sign_minus*.png",
    )
    ap.add_argument("--out-csv", required=True, help="Output CSV path")
    ap.add_argument(
        "--out-diagnostics-csv",
        default=None,
        help="Optional diagnostics CSV path with support_len/confidence_sum",
    )
    ap.add_argument(
        "--start-frame", type=int, default=0, help="Video frame index to start from"
    )
    ap.add_argument(
        "--end-frame",
        type=int,
        default=-1,
        help="Video frame index to stop at (inclusive), -1 means full video",
    )
    ap.add_argument(
        "--min-support",
        type=int,
        default=2,
        help="Minimum run length to accept a raw-frame record",
    )
    ap.add_argument(
        "--log-every",
        type=int,
        default=1000,
        help="Print progress every N processed video frames (0 disables)",
    )
    args = ap.parse_args()

    cap = cv.VideoCapture(args.video)
    if not cap.isOpened():
        raise RuntimeError(f"Error opening video: {args.video}")

    rois = load_rois(args.rois)
    ensure_roi_names(rois, REQUIRED_ROIS)

    bank = load_templates(args.templates_dir)
    if bank is None:
        raise RuntimeError("templates are required for CSV generation")

    frame_count = int(cap.get(cv.CAP_PROP_FRAME_COUNT))
    if frame_count <= 0:
        frame_count = -1

    start_frame = max(0, args.start_frame)
    if args.end_frame >= 0:
        end_frame = args.end_frame
    elif frame_count > 0:
        end_frame = frame_count - 1
    else:
        end_frame = -1

    cap.set(cv.CAP_PROP_POS_FRAMES, start_frame)
    video_frame_idx = start_frame
    n_bits = 48
    observations: list[ObservationRow] = []
    processed_frames = 0
    accepted_count = 0
    decode_fail_count = 0
    preamble_fail_count = 0
    crc_fail_count = 0
    last_success: tuple[int, int, int, int, int, float] | None = None

    while True:
        if end_frame >= 0 and video_frame_idx > end_frame:
            break

        ret, frame = cap.read()
        if not ret:
            break

        processed_frames += 1

        barcode_roi = crop(frame, rois["barcode"])
        _, bits, _ = decode_barcode(barcode_roi, n_bits)
        barcode_dec = parse_barcode_bits(bits)
        if barcode_dec is None:
            decode_fail_count += 1
        elif not barcode_dec.preamble_ok:
            preamble_fail_count += 1
        elif not barcode_dec.crc_ok:
            crc_fail_count += 1
        else:
            x_res = decode_axis_value(frame, rois, "x", bank)
            y_res = decode_axis_value(frame, rois, "y", bank)
            conf_min = min(x_res.confidence, y_res.confidence)

            observations.append(
                ObservationRow(
                    video_frame_idx=video_frame_idx,
                    raw_frame_id=barcode_dec.frame,
                    sx=barcode_dec.sx,
                    sy=barcode_dec.sy,
                    gx=x_res.value,
                    gy=y_res.value,
                    conf_min=conf_min,
                )
            )

            accepted_count += 1
            last_success = (
                barcode_dec.frame,
                barcode_dec.sx,
                barcode_dec.sy,
                x_res.value,
                y_res.value,
                conf_min,
            )

        if args.log_every > 0 and processed_frames % args.log_every == 0:
            sample_text = "sample=none"
            if last_success is not None:
                rf, sx, sy, gx, gy, conf = last_success
                sample_text = (
                    f"sample(raw={rf},sx={sx},sy={sy},gx={gx},gy={gy},conf={conf:.3f})"
                )
            print(
                "progress "
                f"video_frame={video_frame_idx} "
                f"processed={processed_frames} "
                f"accepted={accepted_count} "
                f"decode_fail={decode_fail_count} "
                f"preamble_fail={preamble_fail_count} "
                f"crc_fail={crc_fail_count} "
                f"{sample_text}"
            )

        video_frame_idx += 1

    cap.release()

    aggregated = aggregate_longest_run(
        observations, min_support=max(1, args.min_support)
    )

    print(
        "scan summary "
        f"processed={processed_frames} "
        f"accepted={accepted_count} "
        f"decode_fail={decode_fail_count} "
        f"preamble_fail={preamble_fail_count} "
        f"crc_fail={crc_fail_count} "
        f"min_support={max(1, args.min_support)}"
    )
    print(
        "aggregate summary "
        f"input_observations={len(observations)} "
        f"output_rows={len(aggregated)}"
    )

    out_path = Path(args.out_csv)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["frame", "sx", "sy", "gx", "gy"])
        for row in aggregated:
            writer.writerow([row.frame, row.sx, row.sy, row.gx, row.gy])

    print(f"wrote {len(aggregated)} rows: {out_path}")

    if args.out_diagnostics_csv:
        diag_path = Path(args.out_diagnostics_csv)
        diag_path.parent.mkdir(parents=True, exist_ok=True)
        with diag_path.open("w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["frame", "support_len", "confidence_sum"])
            for row in aggregated:
                writer.writerow(
                    [row.frame, row.support_len, f"{row.confidence_sum:.6f}"]
                )
        print(f"wrote diagnostics: {diag_path}")


if __name__ == "__main__":
    main()
