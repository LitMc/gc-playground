import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots


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
    ap.add_argument("--outdir", required=True, help="Output directory for HTML files")
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
        help="Decimation step for surface rendering",
    )
    ap.add_argument(
        "--quiver-step",
        type=int,
        default=8,
        help="Decimation step for residual vector rendering",
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


def write_html(fig: go.Figure, out: Path) -> None:
    fig.write_html(str(out), include_plotlyjs="cdn", full_html=True)


def compute_residual_fields(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    sx = np.arange(gx_grid.shape[1], dtype=np.float64)
    sy = np.arange(gx_grid.shape[0], dtype=np.float64)
    xx, yy = np.meshgrid(sx, sy)
    expected_gx = xx - 128.0
    expected_gy = yy - 128.0
    dx = gx_grid - expected_gx
    dy = gy_grid - expected_gy
    mag = np.sqrt(dx**2 + dy**2)
    return dx, dy, mag


def write_dashboard(outdir: Path, files: list[tuple[str, str]]) -> None:
    nav_items = "\n".join(
        f'<button class="tab" data-src="{name}">{title}</button>'
        for name, title in files
    )
    links = "\n".join(
        f'<li><a href="{name}" target="_blank" rel="noopener">{title}</a></li>'
        for name, title in files
    )
    first_src = files[0][0] if files else ""
    html = f"""<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Measurement Interactive Dashboard</title>
    <style>
        body {{ margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; color: #1f2937; }}
        header {{ padding: 12px 16px; border-bottom: 1px solid #e5e7eb; }}
        h1 {{ margin: 0; font-size: 18px; }}
        .layout {{ display: grid; grid-template-columns: 300px 1fr; height: calc(100vh - 53px); }}
        .sidebar {{ border-right: 1px solid #e5e7eb; overflow: auto; padding: 12px; }}
        .tabs {{ display: grid; gap: 8px; }}
        .tab {{ text-align: left; border: 1px solid #d1d5db; background: #ffffff; border-radius: 8px; padding: 8px 10px; cursor: pointer; }}
        .tab:hover {{ background: #f9fafb; }}
        .tab.active {{ background: #eef2ff; border-color: #818cf8; }}
        .links {{ margin-top: 14px; font-size: 13px; color: #4b5563; }}
        .links ul {{ margin: 6px 0 0; padding-left: 18px; }}
        .content {{ height: 100%; }}
        iframe {{ width: 100%; height: 100%; border: 0; }}
    </style>
</head>
<body>
    <header>
        <h1>Measurement Interactive Dashboard</h1>
    </header>
    <div class="layout">
        <aside class="sidebar">
            <div class="tabs">{nav_items}</div>
            <div class="links">
                <div>Open directly in new tab:</div>
                <ul>{links}</ul>
            </div>
        </aside>
        <main class="content">
            <iframe id="viewer" src="{first_src}"></iframe>
        </main>
    </div>
    <script>
        const buttons = [...document.querySelectorAll('.tab')];
        const viewer = document.getElementById('viewer');
        const activate = (button) => {{
            buttons.forEach((b) => b.classList.remove('active'));
            button.classList.add('active');
            const src = button.getAttribute('data-src');
            if (src) viewer.setAttribute('src', src);
        }};
        buttons.forEach((button) => button.addEventListener('click', () => activate(button)));
        if (buttons.length > 0) activate(buttons[0]);
    </script>
</body>
</html>
"""
    (outdir / "interactive_dashboard.html").write_text(html, encoding="utf-8")


def save_interactive_heatmaps(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
    outdir: Path,
) -> None:
    fig = make_subplots(
        rows=1,
        cols=2,
        subplot_titles=("gx(sx, sy)", "gy(sx, sy)"),
        horizontal_spacing=0.08,
    )
    zmin = float(min(np.nanmin(gx_grid), np.nanmin(gy_grid)))
    zmax = float(max(np.nanmax(gx_grid), np.nanmax(gy_grid)))

    fig.add_trace(
        go.Heatmap(
            z=gx_grid,
            colorscale="RdBu",
            zmin=zmin,
            zmax=zmax,
            colorbar=dict(title="axis value"),
            hovertemplate="sx=%{x}<br>sy=%{y}<br>gx=%{z}<extra></extra>",
        ),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Heatmap(
            z=gy_grid,
            colorscale="RdBu",
            zmin=zmin,
            zmax=zmax,
            showscale=False,
            hovertemplate="sx=%{x}<br>sy=%{y}<br>gy=%{z}<extra></extra>",
        ),
        row=1,
        col=2,
    )

    fig.update_layout(
        height=560, width=1200, title_text="Measurement heatmaps (raw axis)"
    )
    fig.update_xaxes(title_text="sx", row=1, col=1)
    fig.update_xaxes(title_text="sx", row=1, col=2)
    fig.update_yaxes(title_text="sy", row=1, col=1, scaleanchor="x", scaleratio=1)
    fig.update_yaxes(title_text="sy", row=1, col=2, scaleanchor="x2", scaleratio=1)
    write_html(fig, outdir / "interactive_heatmaps_raw.html")

    centered_x = np.arange(gx_grid.shape[1]) - 128
    centered_y = np.arange(gx_grid.shape[0]) - 128
    fig_centered = make_subplots(
        rows=1,
        cols=2,
        subplot_titles=("gx(sx-128, sy-128)", "gy(sx-128, sy-128)"),
        horizontal_spacing=0.08,
    )
    fig_centered.add_trace(
        go.Heatmap(
            x=centered_x,
            y=centered_y,
            z=gx_grid,
            colorscale="RdBu",
            zmin=zmin,
            zmax=zmax,
            colorbar=dict(title="axis value"),
            hovertemplate="sx-128=%{x}<br>sy-128=%{y}<br>gx=%{z}<extra></extra>",
        ),
        row=1,
        col=1,
    )
    fig_centered.add_trace(
        go.Heatmap(
            x=centered_x,
            y=centered_y,
            z=gy_grid,
            colorscale="RdBu",
            zmin=zmin,
            zmax=zmax,
            showscale=False,
            hovertemplate="sx-128=%{x}<br>sy-128=%{y}<br>gy=%{z}<extra></extra>",
        ),
        row=1,
        col=2,
    )
    fig_centered.update_layout(
        height=560,
        width=1200,
        title_text="Measurement heatmaps (centered axis)",
    )
    fig_centered.update_xaxes(title_text="sx-128", row=1, col=1)
    fig_centered.update_xaxes(title_text="sx-128", row=1, col=2)
    fig_centered.update_yaxes(
        title_text="sy-128", row=1, col=1, scaleanchor="x", scaleratio=1
    )
    fig_centered.update_yaxes(
        title_text="sy-128", row=1, col=2, scaleanchor="x2", scaleratio=1
    )
    write_html(fig_centered, outdir / "interactive_heatmaps_centered.html")


def save_interactive_surfaces(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
    outdir: Path,
    surface_step: int,
) -> None:
    step = max(1, surface_step)
    sx = np.arange(gx_grid.shape[1])[::step]
    sy = np.arange(gx_grid.shape[0])[::step]

    gx_dec = gx_grid[::step, ::step]
    gy_dec = gy_grid[::step, ::step]

    fig_gx = go.Figure(
        data=[
            go.Surface(
                z=gx_dec,
                x=sx,
                y=sy,
                colorscale="RdBu",
                colorbar=dict(title="gx"),
                hovertemplate="sx=%{x}<br>sy=%{y}<br>gx=%{z}<extra></extra>",
            )
        ]
    )
    fig_gx.update_layout(
        title="gx(sx, sy) interactive surface",
        width=1000,
        height=760,
        scene=dict(xaxis_title="sx", yaxis_title="sy", zaxis_title="gx"),
    )
    write_html(fig_gx, outdir / "interactive_surface_gx.html")

    fig_gy = go.Figure(
        data=[
            go.Surface(
                z=gy_dec,
                x=sx,
                y=sy,
                colorscale="RdBu",
                colorbar=dict(title="gy"),
                hovertemplate="sx=%{x}<br>sy=%{y}<br>gy=%{z}<extra></extra>",
            )
        ]
    )
    fig_gy.update_layout(
        title="gy(sx, sy) interactive surface",
        width=1000,
        height=760,
        scene=dict(xaxis_title="sx", yaxis_title="sy", zaxis_title="gy"),
    )
    write_html(fig_gy, outdir / "interactive_surface_gy.html")


def save_interactive_residuals(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
    outdir: Path,
    quiver_step: int,
) -> None:
    sx = np.arange(gx_grid.shape[1], dtype=np.float64)
    sy = np.arange(gx_grid.shape[0], dtype=np.float64)
    xx, yy = np.meshgrid(sx, sy)
    dx, dy, mag = compute_residual_fields(gx_grid, gy_grid)

    fig = make_subplots(rows=1, cols=1, subplot_titles=("Residual magnitude",))
    fig.add_trace(
        go.Heatmap(
            z=mag,
            colorscale="Viridis",
            colorbar=dict(title="|residual|"),
            hovertemplate="sx=%{x}<br>sy=%{y}<br>|residual|=%{z}<extra></extra>",
        )
    )

    step = max(1, quiver_step)
    xs = xx[::step, ::step]
    ys = yy[::step, ::step]
    us = dx[::step, ::step]
    vs = dy[::step, ::step]
    valid = ~(np.isnan(us) | np.isnan(vs))

    x_lines: list[float | None] = []
    y_lines: list[float | None] = []
    scale = 0.6
    for x0, y0, u, v in zip(xs[valid], ys[valid], us[valid], vs[valid], strict=False):
        x_lines.extend([float(x0), float(x0 + scale * u), None])
        y_lines.extend([float(y0), float(y0 + scale * v), None])

    fig.add_trace(
        go.Scatter(
            x=x_lines,
            y=y_lines,
            mode="lines",
            line=dict(color="white", width=1),
            name="residual vectors",
            hoverinfo="skip",
        )
    )
    fig.update_layout(
        title="Residual map and vector field",
        width=980,
        height=760,
    )
    fig.update_xaxes(title_text="sx")
    fig.update_yaxes(title_text="sy", scaleanchor="x", scaleratio=1)
    write_html(fig, outdir / "interactive_residuals.html")


def save_interactive_residual_surface(
    gx_grid: np.ndarray,
    gy_grid: np.ndarray,
    outdir: Path,
    surface_step: int,
) -> None:
    step = max(1, surface_step)
    sx = np.arange(gx_grid.shape[1])[::step]
    sy = np.arange(gx_grid.shape[0])[::step]

    dx, dy, mag = compute_residual_fields(gx_grid, gy_grid)
    dx_dec = dx[::step, ::step]
    dy_dec = dy[::step, ::step]
    mag_dec = mag[::step, ::step]
    custom_data = np.dstack((dx_dec, dy_dec))

    fig = go.Figure(
        data=[
            go.Surface(
                z=mag_dec,
                x=sx,
                y=sy,
                customdata=custom_data,
                colorscale="Viridis",
                colorbar=dict(title="|residual|"),
                hovertemplate=(
                    "sx=%{x}<br>sy=%{y}<br>|residual|=%{z:.3f}"
                    "<br>dx=%{customdata[0]:.3f}<br>dy=%{customdata[1]:.3f}<extra></extra>"
                ),
            )
        ]
    )
    fig.update_layout(
        title="|residual|(sx, sy) interactive surface",
        width=1000,
        height=760,
        scene=dict(
            xaxis_title="sx",
            yaxis_title="sy",
            zaxis_title="|residual|",
        ),
    )
    write_html(fig, outdir / "interactive_surface_residual_mag.html")


def save_interactive_diagnostics(
    diagnostics_rows: list[DiagnosticsRow],
    frame_to_coord: dict[int, tuple[int, int]],
    outdir: Path,
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

    fig = make_subplots(
        rows=1,
        cols=2,
        subplot_titles=("support_len map", "confidence_sum map"),
        horizontal_spacing=0.08,
    )
    fig.add_trace(
        go.Heatmap(
            z=support,
            colorscale="Magma",
            colorbar=dict(title="support_len"),
            hovertemplate="sx=%{x}<br>sy=%{y}<br>support_len=%{z}<extra></extra>",
        ),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Heatmap(
            z=conf,
            colorscale="Magma",
            showscale=False,
            hovertemplate="sx=%{x}<br>sy=%{y}<br>confidence_sum=%{z}<extra></extra>",
        ),
        row=1,
        col=2,
    )
    fig.update_layout(height=560, width=1200, title_text="Diagnostics maps")
    fig.update_xaxes(title_text="sx", row=1, col=1)
    fig.update_xaxes(title_text="sx", row=1, col=2)
    fig.update_yaxes(title_text="sy", row=1, col=1, scaleanchor="x", scaleratio=1)
    fig.update_yaxes(title_text="sy", row=1, col=2, scaleanchor="x2", scaleratio=1)
    write_html(fig, outdir / "interactive_diagnostics.html")


def save_interactive_scatter_and_slices(
    rows: list[ReadingRow],
    outdir: Path,
    grid_size: int,
) -> None:
    sx = np.array([r.sx for r in rows], dtype=np.float64)
    sy = np.array([r.sy for r in rows], dtype=np.float64)
    gx = np.array([r.gx for r in rows], dtype=np.float64)
    gy = np.array([r.gy for r in rows], dtype=np.float64)

    fig = make_subplots(
        rows=1,
        cols=2,
        subplot_titles=("sx vs gx", "sy vs gy"),
        horizontal_spacing=0.1,
    )
    fig.add_trace(
        go.Scattergl(
            x=sx,
            y=gx,
            mode="markers",
            marker=dict(size=3, opacity=0.25),
            name="sx-gx",
            hovertemplate="sx=%{x}<br>gx=%{y}<extra></extra>",
        ),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Scatter(
            x=[0, grid_size - 1],
            y=[-128, grid_size - 129],
            mode="lines",
            line=dict(color="red", dash="dash"),
            showlegend=False,
            hoverinfo="skip",
        ),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Scattergl(
            x=sy,
            y=gy,
            mode="markers",
            marker=dict(size=3, opacity=0.25),
            name="sy-gy",
            hovertemplate="sy=%{x}<br>gy=%{y}<extra></extra>",
        ),
        row=1,
        col=2,
    )
    fig.add_trace(
        go.Scatter(
            x=[0, grid_size - 1],
            y=[-128, grid_size - 129],
            mode="lines",
            line=dict(color="red", dash="dash"),
            showlegend=False,
            hoverinfo="skip",
        ),
        row=1,
        col=2,
    )
    fig.update_layout(height=560, width=1200, title_text="Scatter pairs")
    fig.update_xaxes(title_text="sx", row=1, col=1)
    fig.update_yaxes(title_text="gx", row=1, col=1)
    fig.update_xaxes(title_text="sy", row=1, col=2)
    fig.update_yaxes(title_text="gy", row=1, col=2)
    write_html(fig, outdir / "interactive_scatter_pairs.html")

    sy_lines = [0, 64, 128, 192, 255]
    sx_lines = [0, 64, 128, 192, 255]
    gx_by_sy: dict[int, list[ReadingRow]] = {}
    gy_by_sx: dict[int, list[ReadingRow]] = {}
    for row in rows:
        gx_by_sy.setdefault(row.sy, []).append(row)
        gy_by_sx.setdefault(row.sx, []).append(row)

    slice_fig = make_subplots(
        rows=1,
        cols=2,
        subplot_titles=("gx slices at fixed sy", "gy slices at fixed sx"),
        horizontal_spacing=0.08,
    )

    for sy_line in sy_lines:
        if sy_line not in gx_by_sy:
            continue
        line_rows = sorted(gx_by_sy[sy_line], key=lambda r: r.sx)
        slice_fig.add_trace(
            go.Scatter(
                x=[r.sx for r in line_rows],
                y=[r.gx for r in line_rows],
                mode="lines",
                name=f"sy={sy_line}",
                hovertemplate="sx=%{x}<br>gx=%{y}<extra></extra>",
            ),
            row=1,
            col=1,
        )

    slice_fig.add_trace(
        go.Scatter(
            x=[0, grid_size - 1],
            y=[-128, grid_size - 129],
            mode="lines",
            line=dict(color="black", dash="dash"),
            name="ideal",
            hoverinfo="skip",
        ),
        row=1,
        col=1,
    )

    for sx_line in sx_lines:
        if sx_line not in gy_by_sx:
            continue
        line_rows = sorted(gy_by_sx[sx_line], key=lambda r: r.sy)
        slice_fig.add_trace(
            go.Scatter(
                x=[r.sy for r in line_rows],
                y=[r.gy for r in line_rows],
                mode="lines",
                name=f"sx={sx_line}",
                hovertemplate="sy=%{x}<br>gy=%{y}<extra></extra>",
            ),
            row=1,
            col=2,
        )

    slice_fig.add_trace(
        go.Scatter(
            x=[0, grid_size - 1],
            y=[-128, grid_size - 129],
            mode="lines",
            line=dict(color="black", dash="dash"),
            showlegend=False,
            hoverinfo="skip",
        ),
        row=1,
        col=2,
    )

    slice_fig.update_layout(height=560, width=1300, title_text="Slice curves")
    slice_fig.update_xaxes(title_text="sx", row=1, col=1)
    slice_fig.update_yaxes(title_text="gx", row=1, col=1)
    slice_fig.update_xaxes(title_text="sy", row=1, col=2)
    slice_fig.update_yaxes(title_text="gy", row=1, col=2)
    write_html(slice_fig, outdir / "interactive_slices.html")


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
    print(
        "grid summary "
        f"rows={len(readings)} "
        f"filled={filled}/{total} "
        f"missing={total - filled} "
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

    save_interactive_heatmaps(gx_grid, gy_grid, outdir)
    save_interactive_surfaces(gx_grid, gy_grid, outdir, args.surface_step)
    save_interactive_residuals(gx_grid, gy_grid, outdir, args.quiver_step)
    save_interactive_residual_surface(gx_grid, gy_grid, outdir, args.surface_step)
    save_interactive_scatter_and_slices(readings, outdir, args.grid_size)

    dashboard_files: list[tuple[str, str]] = [
        ("interactive_surface_gx.html", "Surface GX"),
        ("interactive_surface_gy.html", "Surface GY"),
        ("interactive_surface_residual_mag.html", "Surface Residual |R|"),
        ("interactive_heatmaps_raw.html", "Heatmaps Raw"),
        ("interactive_heatmaps_centered.html", "Heatmaps Centered"),
        ("interactive_residuals.html", "Residuals"),
        ("interactive_scatter_pairs.html", "Scatter Pairs"),
        ("interactive_slices.html", "Slices"),
    ]

    if args.diagnostics:
        diagnostics_rows = read_diagnostics(Path(args.diagnostics))
        save_interactive_diagnostics(
            diagnostics_rows,
            frame_to_coord,
            outdir,
            args.grid_size,
        )
        dashboard_files.append(("interactive_diagnostics.html", "Diagnostics"))

    write_dashboard(outdir, dashboard_files)

    print(f"wrote interactive plots to: {outdir}")


if __name__ == "__main__":
    main()
