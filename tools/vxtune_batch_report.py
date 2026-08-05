#!/usr/bin/env python3
"""Generate VX Tune batch summary tables and contour SVGs.

The input is a directory produced by VxTuneBatchHarness. The script reads
vxtune_batch_metrics.csv and each per-render debug CSV, then writes:

- vxtune_batch_summary.md
- contour SVGs for the worst harmed and worst output-error renders
"""

from __future__ import annotations

import argparse
import csv
import html
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean


def f(value: str, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def nearest_chromatic_error(cents: float) -> float:
    return cents - round(cents / 100.0) * 100.0


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def avg(rows: list[dict[str, str]], column: str) -> float:
    return mean(f(row.get(column, "")) for row in rows) if rows else 0.0


def score_sort_key(score: str) -> tuple[int, object]:
    try:
        return (0, int(score))
    except (TypeError, ValueError):
        return (1, score or "")


def markdown_table(headers: list[str], rows: list[list[object]]) -> str:
    out = ["| " + " | ".join(headers) + " |"]
    out.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        out.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(out)


def load_debug(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    return load_csv(path)


def simplify_points(points: list[tuple[float, float]], max_points: int = 900) -> list[tuple[float, float]]:
    if len(points) <= max_points:
        return points
    step = max(1, math.ceil(len(points) / max_points))
    return points[::step]


def polyline(points: list[tuple[float, float]]) -> str:
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


def render_contour_svg(row: dict[str, str], output_path: Path) -> bool:
    debug_path = Path(row["debug_csv"])
    frames = load_debug(debug_path)
    if not frames:
        return False

    width = 1200
    height = 520
    margin_left = 64
    margin_right = 24
    margin_top = 38
    margin_bottom = 54
    plot_w = width - margin_left - margin_right
    plot_h = height - margin_top - margin_bottom

    times = [f(frame["seconds"]) for frame in frames]
    if not times:
        return False
    t_min = min(times)
    t_max = max(times)
    if t_max <= t_min:
        t_max = t_min + 1.0

    series: dict[str, list[tuple[float, float]]] = {
        "input error": [],
        "output error": [],
        "correction": [],
    }
    confidence: list[tuple[float, float]] = []

    y_min = -80.0
    y_max = 80.0

    def sx(t: float) -> float:
        return margin_left + (t - t_min) / (t_max - t_min) * plot_w

    def sy(v: float) -> float:
        v = max(y_min, min(y_max, v))
        return margin_top + (y_max - v) / (y_max - y_min) * plot_h

    for frame in frames:
        t = f(frame["seconds"])
        centre = f(frame["centre_cents"])
        residual = f(frame["residual_cents"])
        corrected = f(frame["corrected_cents"])
        correction = f(frame["correction_cents"])
        conf = f(frame["confidence"])
        detected = centre + residual
        series["input error"].append((sx(t), sy(nearest_chromatic_error(detected))))
        series["output error"].append((sx(t), sy(nearest_chromatic_error(corrected))))
        series["correction"].append((sx(t), sy(correction)))
        confidence.append((sx(t), margin_top + (1.0 - max(0.0, min(1.0, conf))) * plot_h))

    for key in series:
        series[key] = simplify_points(series[key])
    confidence = simplify_points(confidence)

    title = (
        f"{row['id']} / {row['preset']} / score {row['score']} / "
        f"improvement {f(row['median_abs_improvement_cents']):.2f}c"
    )
    escaped_title = html.escape(title)
    zero_y = sy(0.0)
    grid_lines = []
    for cents in range(-50, 51, 25):
        y = sy(float(cents))
        grid_lines.append(
            f'<line x1="{margin_left}" y1="{y:.1f}" x2="{width - margin_right}" '
            f'y2="{y:.1f}" stroke="#e7e2d8" stroke-width="1"/>'
        )
        grid_lines.append(
            f'<text x="{margin_left - 10}" y="{y + 4:.1f}" text-anchor="end" '
            f'font-size="12" fill="#726b62">{cents}</text>'
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#fbfaf7"/>
<text x="{margin_left}" y="24" font-size="18" font-family="Arial, sans-serif" fill="#26231f">{escaped_title}</text>
<rect x="{margin_left}" y="{margin_top}" width="{plot_w}" height="{plot_h}" fill="#fffefb" stroke="#d8d0c4"/>
{''.join(grid_lines)}
<line x1="{margin_left}" y1="{zero_y:.1f}" x2="{width - margin_right}" y2="{zero_y:.1f}" stroke="#8f877d" stroke-width="1.3"/>
<polyline points="{polyline(confidence)}" fill="none" stroke="#d5d1cc" stroke-width="2" opacity="0.8"/>
<polyline points="{polyline(series['input error'])}" fill="none" stroke="#6b7280" stroke-width="2"/>
<polyline points="{polyline(series['output error'])}" fill="none" stroke="#2563eb" stroke-width="2"/>
<polyline points="{polyline(series['correction'])}" fill="none" stroke="#d97706" stroke-width="2"/>
<text x="{margin_left}" y="{height - 22}" font-size="13" font-family="Arial, sans-serif" fill="#4b5563">input error</text>
<line x1="{margin_left + 76}" y1="{height - 26}" x2="{margin_left + 126}" y2="{height - 26}" stroke="#6b7280" stroke-width="2"/>
<text x="{margin_left + 150}" y="{height - 22}" font-size="13" font-family="Arial, sans-serif" fill="#2563eb">output error</text>
<line x1="{margin_left + 238}" y1="{height - 26}" x2="{margin_left + 288}" y2="{height - 26}" stroke="#2563eb" stroke-width="2"/>
<text x="{margin_left + 312}" y="{height - 22}" font-size="13" font-family="Arial, sans-serif" fill="#d97706">correction</text>
<line x1="{margin_left + 382}" y1="{height - 26}" x2="{margin_left + 432}" y2="{height - 26}" stroke="#d97706" stroke-width="2"/>
<text x="{width - margin_right}" y="{height - 22}" text-anchor="end" font-size="13" font-family="Arial, sans-serif" fill="#726b62">time: {t_min:.2f}s to {t_max:.2f}s, y-axis cents vs nearest chromatic</text>
</svg>
""",
        encoding="utf-8",
    )
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("batch_dir", type=Path)
    parser.add_argument("--top", type=int, default=12)
    args = parser.parse_args()

    metrics_path = args.batch_dir / "vxtune_batch_metrics.csv"
    if not metrics_path.exists():
        raise SystemExit(f"Missing metrics CSV: {metrics_path}")

    rows = load_csv(metrics_path)
    ok_rows = [row for row in rows if row.get("status") == "ok"]

    by_score_preset: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in ok_rows:
        by_score_preset[(row["score"], row["preset"])].append(row)

    summary_rows: list[list[object]] = []
    for score, preset in sorted(by_score_preset, key=lambda key: (score_sort_key(key[0]), key[1])):
        group = by_score_preset[(score, preset)]
        summary_rows.append(
            [
                score,
                preset,
                len(group),
                f"{avg(group, 'median_abs_improvement_cents'):.2f}c",
                f"{avg(group, 'median_abs_output_error_cents'):.2f}c",
                f"{avg(group, 'mean_abs_correction_cents'):.2f}c",
                f"{avg(group, 'mean_musical_authority'):.2f}",
                f"{avg(group, 'mean_stable_boost_authority'):.2f}",
                f"{avg(group, 'mean_near_correct_authority'):.2f}",
                f"{avg(group, 'mean_low_confidence_abs_correction_cents'):.2f}c",
                f"{avg(group, 'vibrato_extent_ratio'):.2f}",
            ]
        )

    harmed = sorted(ok_rows, key=lambda row: f(row["median_abs_improvement_cents"]))[: args.top]
    worst = sorted(ok_rows, key=lambda row: f(row["median_abs_output_error_cents"]), reverse=True)[
        : args.top
    ]

    contour_dir = args.batch_dir / "contours"
    plotted: list[tuple[str, str, Path]] = []
    seen: set[tuple[str, str]] = set()
    for category, selected in (("most_harmed", harmed), ("worst_output_error", worst)):
        for row in selected:
            key = (row["id"], row["preset"])
            if key in seen:
                continue
            seen.add(key)
            svg = contour_dir / category / f"{row['id']}_{row['preset']}.svg"
            if render_contour_svg(row, svg):
                plotted.append((category, f"{row['id']} {row['preset']}", svg))

    report_path = args.batch_dir / "vxtune_batch_summary.md"
    report_path.write_text(
        "\n".join(
            [
                "# VX Tune Batch Summary",
                "",
                f"Batch directory: `{args.batch_dir}`",
                f"Rows: {len(rows)} total, {len(ok_rows)} ok",
                "",
                "## Aggregate Metrics",
                "",
                markdown_table(
                    [
                        "Score",
                        "Preset",
                        "N",
                        "Mean Improvement",
                        "Mean Output Error",
                        "Mean Correction",
                        "Mean Authority",
                        "Stable Boost",
                        "Near-Correct",
                        "Low-Conf Correction",
                        "Vibrato Ratio",
                    ],
                    summary_rows,
                ),
                "",
                "## Most Harmed",
                "",
                markdown_table(
                    ["ID", "Preset", "Score", "Improvement", "Output Error", "Mean Correction"],
                    [
                        [
                            row["id"],
                            row["preset"],
                            row["score"],
                            f"{f(row['median_abs_improvement_cents']):.2f}c",
                            f"{f(row['median_abs_output_error_cents']):.2f}c",
                            f"{f(row['mean_abs_correction_cents']):.2f}c",
                        ]
                        for row in harmed
                    ],
                ),
                "",
                "## Worst Output Error",
                "",
                markdown_table(
                    ["ID", "Preset", "Score", "Output Error", "Improvement", "Mean Correction"],
                    [
                        [
                            row["id"],
                            row["preset"],
                            row["score"],
                            f"{f(row['median_abs_output_error_cents']):.2f}c",
                            f"{f(row['median_abs_improvement_cents']):.2f}c",
                            f"{f(row['mean_abs_correction_cents']):.2f}c",
                        ]
                        for row in worst
                    ],
                ),
                "",
                "## Contours",
                "",
                *[
                    f"- {category}: {label} -> `{svg.relative_to(args.batch_dir)}`"
                    for category, label, svg in plotted
                ],
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(f"Wrote {report_path}")
    print(f"Wrote {len(plotted)} contour SVG(s) under {contour_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
