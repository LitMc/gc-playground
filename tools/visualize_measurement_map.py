import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import cm


@dataclass(frozen=True)
class ReadingRow:
    frame: int
    sx: int
    sy: int
    gx: int
    gy: int


@dataclass(frozen=True)
class DiagnosticsRow:
    frame: int
    support_len: int
    confidence_sum: float


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="Path to readings.csv")
    ap.add_argument(
        "--diagnostics",
        default=None,
        help="Optional path to readings_diagnostics.csv",
    )
    ap.add_argument("--outdir", required=True, help="Output directory for PNGs")
    ap.add_argument(
        "--grid-size",
        type=int,
        default=256,
        help="Grid size for sx/sy axis (default: 256)",
    )
    ap.add_argument(
        "--surface-step",
        type=int,
        default=2,
        help="Decimation step for 3D surface plotting",
    )
    ap.add_argument(
        "--quiver-step",
        type=int,
        default=8,
        help="Decimation step for residual quiver plotting",
    )
    ap.add_argument(
        "--dpi",
        type=int,
        default=180,
        help="Saved image dpi",
    )
    ap.add_argument(
        "--skip-surface",
        action="store_true",
        help="Skip 3D surface plots",
    )
    ap.add_argument(
        "--missing-limit",
        type=int,
        default=32,
        help="Maximum number of missing (sx,sy) coordinates to print",
    )
    return ap.parse_args()


def read_readings(path: Path) -> list[ReadingRow]:
    rows: list[ReadingRow] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        expected = {"frame", "sx", "sy", "gx", "gy"}
        if set(reader.fieldnames or []) != expected:
            raise ValueError(
                f"Unexpected headers in {path}: {reader.fieldnames}, expected {sorted(expected)}"
            )
        for row in reader:
            rows.append(
                ReadingRow(
                    frame=int(row["frame"]),
                    sx=int(row["sx"]),
                    sy=int(row["sy"]),
                    gx=int(row["gx"]),
                    gy=int(row["gy"]),
                )
            )
    if not rows:
        raise ValueError(f"No rows found: {path}")
    return rows


def read_diagnostics(path: Path) -> list[DiagnosticsRow]:
    rows: list[DiagnosticsRow] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        expected = {"frame", "support_len", "confidence_sum"}
        if set(reader.fieldnames or []) != expected:
            raise ValueError(
                f"Unexpected headers in {path}: {reader.fieldnames}, expected {sorted(expected)}"
            )
        for row in reader:
            rows.append(
                DiagnosticsRow(
                    frame=int(row["frame"]),
                    support_len=int(row["support_len"]),
                    confidence_sum=float(row["confidence_sum"]),
                )
            )
    return rows


def build_grids(
    rows: list[ReadingRow],
    grid_size: int,
) -> tuple[np.ndarray, np.ndarray, dict[int, tuple[int, int]], int, int]:
    gx_grid = np.full((grid_size, grid_size), np.nan, dtype=np.float64)
    gy_grid = np.full((grid_size, grid_size), np.nan, dtype=np.float64)
    frame_to_coord: dict[int, tuple[int, int]] = {}
    duplicate_count = 0
    out_of_range_count = 0

    for row in rows:
        if not (0 <= row.sx < grid_size and 0 <= row.sy < grid_size):
            out_of_range_count += 1
            continue
        if not np.isnan(gx_grid[row.sy, row.sx]):
            duplicate_count += 1
        gx_grid[row.sy, row.sx] = row.gx
        gy_grid[row.sy, row.sx] = row.gy
        frame_to_coord[row.frame] = (row.sx, row.sy)

    return gx_grid, gy_grid, frame_to_coord, duplicate_count, out_of_range_count


def find_missing_coords(
    gx_grid: np.ndarray, gy_grid: np.ndarray
) -> list[tuple[int, int]]:
    missing_mask = np.isnan(gx_grid) | np.isnan(gy_grid)
    coords = np.argwhere(missing_mask)
    return [(int(x), int(y)) for y, x in coords]


def save_heatmaps(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
    outdir: Path,
    dpi: int,
    centered: bool,
) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)

    if centered:
        extent = (-128, 127, -128, 127)
        xlabel = "sx-128"
        ylabel = "sy-128"
        suffix = "centered"
    else:
        max_index = gx_grid.shape[1] - 1
        extent = (0, max_index, 0, max_index)
        xlabel = "sx"
        ylabel = "sy"
        suffix = "raw"

    vmin = min(np.nanmin(gx_grid), np.nanmin(gy_grid))
    vmax = max(np.nanmax(gx_grid), np.nanmax(gy_grid))

    im0 = axes[0].imshow(
        gx_grid,
        origin="lower",
        cmap="coolwarm",
        extent=extent,
        vmin=vmin,
        vmax=vmax,
        aspect="equal",
    )
    axes[0].set_title("gx(sx, sy)")
    axes[0].set_xlabel(xlabel)
    axes[0].set_ylabel(ylabel)

    im1 = axes[1].imshow(
        gy_grid,
        origin="lower",
        cmap="coolwarm",
        extent=extent,
        vmin=vmin,
        vmax=vmax,
        aspect="equal",
    )
    axes[1].set_title("gy(sx, sy)")
    axes[1].set_xlabel(xlabel)
    axes[1].set_ylabel(ylabel)

    cbar = fig.colorbar(im1, ax=axes.ravel().tolist(), shrink=0.9)
    cbar.set_label("axis value")

    out = outdir / f"map_heatmaps_{suffix}.png"
    fig.savefig(out, dpi=dpi)
    plt.close(fig)


def save_residual_quiver(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
    outdir: Path,
    dpi: int,
    quiver_step: int,
) -> None:
    grid_size = gx_grid.shape[0]
    sx = np.arange(grid_size, dtype=np.float64)
    sy = np.arange(grid_size, dtype=np.float64)
    xx, yy = np.meshgrid(sx, sy)

    expected_gx = xx - 128.0
    expected_gy = yy - 128.0
    dx = gx_grid - expected_gx
    dy = gy_grid - expected_gy
    mag = np.sqrt(dx**2 + dy**2)

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)

    im = axes[0].imshow(
        mag,
        origin="lower",
        cmap="viridis",
        extent=(0, grid_size - 1, 0, grid_size - 1),
        aspect="equal",
    )
    axes[0].set_title("|residual| against ideal (sx-128, sy-128)")
    axes[0].set_xlabel("sx")
    axes[0].set_ylabel("sy")
    cbar = fig.colorbar(im, ax=axes[0], shrink=0.9)
    cbar.set_label("residual magnitude")

    xs = xx[::quiver_step, ::quiver_step]
    ys = yy[::quiver_step, ::quiver_step]
    us = dx[::quiver_step, ::quiver_step]
    vs = dy[::quiver_step, ::quiver_step]
    valid = ~(np.isnan(us) | np.isnan(vs))
    axes[1].quiver(xs[valid], ys[valid], us[valid], vs[valid], angles="xy")
    axes[1].set_title("Residual vector field")
    axes[1].set_xlabel("sx")
    axes[1].set_ylabel("sy")
    axes[1].set_aspect("equal")
    axes[1].set_xlim(0, grid_size - 1)
    axes[1].set_ylim(0, grid_size - 1)

    out = outdir / "map_residuals.png"
    fig.savefig(out, dpi=dpi)
    plt.close(fig)


def save_surface(
    z_grid: np.ndarray,
    out: Path,
    z_label: str,
    dpi: int,
    surface_step: int,
) -> None:
    grid_size = z_grid.shape[0]
    sx = np.arange(grid_size, dtype=np.float64)
    sy = np.arange(grid_size, dtype=np.float64)
    xx, yy = np.meshgrid(sx, sy)

    xs = xx[::surface_step, ::surface_step]
    ys = yy[::surface_step, ::surface_step]
    zs = np.ma.masked_invalid(z_grid[::surface_step, ::surface_step])

    fig = plt.figure(figsize=(9, 7), constrained_layout=True)
    ax = fig.add_subplot(111, projection="3d")
    surf = ax.plot_surface(
        xs,
        ys,
        zs,
        cmap=cm.coolwarm,
        linewidth=0,
        antialiased=False,
    )
    ax.set_title(f"{z_label}(sx, sy) surface")
    ax.set_xlabel("sx")
    ax.set_ylabel("sy")
    ax.set_zlabel(z_label)
    fig.colorbar(surf, shrink=0.7, aspect=15)
    fig.savefig(out, dpi=dpi)
    plt.close(fig)


def save_scatter_and_slices(
    rows: list[ReadingRow],
    outdir: Path,
    dpi: int,
    grid_size: int,
) -> None:
    sx = np.array([r.sx for r in rows], dtype=np.float64)
    sy = np.array([r.sy for r in rows], dtype=np.float64)
    gx = np.array([r.gx for r in rows], dtype=np.float64)
    gy = np.array([r.gy for r in rows], dtype=np.float64)

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)
    axes[0].scatter(sx, gx, s=4, alpha=0.25)
    axes[0].plot([0, grid_size - 1], [-128, grid_size - 129], "r--", linewidth=1)
    axes[0].set_title("sx vs gx")
    axes[0].set_xlabel("sx")
    axes[0].set_ylabel("gx")

    axes[1].scatter(sy, gy, s=4, alpha=0.25)
    axes[1].plot([0, grid_size - 1], [-128, grid_size - 129], "r--", linewidth=1)
    axes[1].set_title("sy vs gy")
    axes[1].set_xlabel("sy")
    axes[1].set_ylabel("gy")

    fig.savefig(outdir / "map_scatter_pairs.png", dpi=dpi)
    plt.close(fig)

    sy_lines = [0, 64, 128, 192, 255]
    sx_lines = [0, 64, 128, 192, 255]

    gx_by_sy: dict[int, list[ReadingRow]] = {}
    gy_by_sx: dict[int, list[ReadingRow]] = {}
    for row in rows:
        gx_by_sy.setdefault(row.sy, []).append(row)
        gy_by_sx.setdefault(row.sx, []).append(row)

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)
    for sy_line in sy_lines:
        if sy_line not in gx_by_sy:
            continue
        line_rows = sorted(gx_by_sy[sy_line], key=lambda r: r.sx)
        axes[0].plot(
            [r.sx for r in line_rows],
            [r.gx for r in line_rows],
            label=f"sy={sy_line}",
        )
    axes[0].plot([0, grid_size - 1], [-128, grid_size - 129], "k--", linewidth=1)
    axes[0].set_title("gx slices at fixed sy")
    axes[0].set_xlabel("sx")
    axes[0].set_ylabel("gx")
    axes[0].legend(loc="best", fontsize=8)

    for sx_line in sx_lines:
        if sx_line not in gy_by_sx:
            continue
        line_rows = sorted(gy_by_sx[sx_line], key=lambda r: r.sy)
        axes[1].plot(
            [r.sy for r in line_rows],
            [r.gy for r in line_rows],
            label=f"sx={sx_line}",
        )
    axes[1].plot([0, grid_size - 1], [-128, grid_size - 129], "k--", linewidth=1)
    axes[1].set_title("gy slices at fixed sx")
    axes[1].set_xlabel("sy")
    axes[1].set_ylabel("gy")
    axes[1].legend(loc="best", fontsize=8)

    fig.savefig(outdir / "map_slices.png", dpi=dpi)
    plt.close(fig)


def save_diagnostics_maps(
    diagnostics_rows: list[DiagnosticsRow],
    frame_to_coord: dict[int, tuple[int, int]],
    outdir: Path,
    dpi: int,
    grid_size: int,
) -> None:
    support = np.full((grid_size, grid_size), np.nan, dtype=np.float64)
    conf = np.full((grid_size, grid_size), np.nan, dtype=np.float64)

    for row in diagnostics_rows:
        coord = frame_to_coord.get(row.frame)
        if coord is None:
            continue
        sx, sy = coord
        support[sy, sx] = row.support_len
        conf[sy, sx] = row.confidence_sum

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)
    im0 = axes[0].imshow(
        support,
        origin="lower",
        cmap="magma",
        extent=(0, grid_size - 1, 0, grid_size - 1),
        aspect="equal",
    )
    axes[0].set_title("support_len map")
    axes[0].set_xlabel("sx")
    axes[0].set_ylabel("sy")
    fig.colorbar(im0, ax=axes[0], shrink=0.9)

    im1 = axes[1].imshow(
        conf,
        origin="lower",
        cmap="magma",
        extent=(0, grid_size - 1, 0, grid_size - 1),
        aspect="equal",
    )
    axes[1].set_title("confidence_sum map")
    axes[1].set_xlabel("sx")
    axes[1].set_ylabel("sy")
    fig.colorbar(im1, ax=axes[1], shrink=0.9)

    fig.savefig(outdir / "map_diagnostics.png", dpi=dpi)
    plt.close(fig)


def main() -> None:
    args = parse_args()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    readings = read_readings(Path(args.input))
    gx_grid, gy_grid, frame_to_coord, duplicate_count, out_of_range_count = build_grids(
        readings,
        args.grid_size,
    )
    missing_coords = find_missing_coords(gx_grid, gy_grid)

    filled = int(np.count_nonzero(~np.isnan(gx_grid)))
    total = args.grid_size * args.grid_size
    missing = total - filled
    print(
        "grid summary "
        f"rows={len(readings)} "
        f"filled={filled}/{total} "
        f"missing={missing} "
        f"duplicates={duplicate_count} "
        f"out_of_range={out_of_range_count}"
    )
    if missing_coords:
        limit = max(0, args.missing_limit)
        shown = missing_coords[:limit]
        if shown:
            sample_text = ", ".join(f"({sx},{sy})" for sx, sy in shown)
            print(
                f"missing coords ({len(missing_coords)} total, showing {len(shown)}): {sample_text}"
            )
        if len(missing_coords) > limit:
            print(
                f"... {len(missing_coords) - limit} more missing coordinates are omitted"
            )
    else:
        print("missing coords: none")
    print(
        "value range "
        f"gx=[{np.nanmin(gx_grid):.1f},{np.nanmax(gx_grid):.1f}] "
        f"gy=[{np.nanmin(gy_grid):.1f},{np.nanmax(gy_grid):.1f}]"
    )

    save_heatmaps(gx_grid, gy_grid, outdir, args.dpi, centered=False)
    save_heatmaps(gx_grid, gy_grid, outdir, args.dpi, centered=True)
    save_residual_quiver(gx_grid, gy_grid, outdir, args.dpi, args.quiver_step)
    save_scatter_and_slices(readings, outdir, args.dpi, args.grid_size)

    if not args.skip_surface:
        save_surface(
            gx_grid,
            outdir / "map_surface_gx.png",
            "gx",
            args.dpi,
            max(1, args.surface_step),
        )
        save_surface(
            gy_grid,
            outdir / "map_surface_gy.png",
            "gy",
            args.dpi,
            max(1, args.surface_step),
        )

    if args.diagnostics:
        diagnostics = read_diagnostics(Path(args.diagnostics))
        save_diagnostics_maps(
            diagnostics,
            frame_to_coord,
            outdir,
            args.dpi,
            args.grid_size,
        )

    print(f"wrote plots to: {outdir}")


if __name__ == "__main__":
    main()
