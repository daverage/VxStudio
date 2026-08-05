#!/usr/bin/env python3
"""Add MAST reference-alignment musicality metrics to a VX Tune batch.

This is an offline development tool. It uses MAST's precomputed chroma arrays to
align each student performance to the closest matching reference recording for
the same melodic pattern, then compares VX Tune debug contours against the
aligned reference pitch class.
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path

import numpy as np


def f(value: str, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def stem_pattern(stem: str) -> str:
    # 510_mel1_per104558 -> 510_mel1
    parts = stem.split("_")
    return "_".join(parts[:2]) if len(parts) >= 2 else stem


def chroma_path(chroma_dir: Path, stem: str) -> Path:
    return chroma_dir / f"{stem}.chroma.npy"


def load_chroma_12(path: Path) -> np.ndarray:
    chroma = np.load(path).astype(np.float32)
    chroma = np.nan_to_num(chroma, nan=0.0, posinf=0.0, neginf=0.0)
    if chroma.shape[0] == 24:
        chroma = chroma.reshape(12, 2, chroma.shape[1]).sum(axis=1)
    elif chroma.shape[0] != 12:
        raise ValueError(f"Unexpected chroma shape {chroma.shape} for {path}")
    norms = np.linalg.norm(chroma, axis=0, keepdims=True)
    normalized = chroma / np.maximum(norms, 1.0e-6)
    return np.nan_to_num(normalized, nan=0.0, posinf=0.0, neginf=0.0)


def cosine_cost_matrix(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    # a/b shape: 12 x frames, normalized columns.
    with np.errstate(divide="ignore", over="ignore", invalid="ignore"):
        similarity = np.nan_to_num(a.T @ b, nan=0.0, posinf=0.0, neginf=0.0)
    similarity = np.clip(similarity, 0.0, 1.0)
    return 1.0 - similarity


def dtw_path(cost: np.ndarray) -> tuple[float, list[tuple[int, int]]]:
    n, m = cost.shape
    acc = np.full((n, m), np.inf, dtype=np.float64)
    back = np.zeros((n, m, 2), dtype=np.int16)
    acc[0, 0] = cost[0, 0]
    for i in range(n):
        for j in range(m):
            if i == 0 and j == 0:
                continue
            candidates: list[tuple[float, int, int]] = []
            if i > 0:
                candidates.append((acc[i - 1, j], i - 1, j))
            if j > 0:
                candidates.append((acc[i, j - 1], i, j - 1))
            if i > 0 and j > 0:
                candidates.append((acc[i - 1, j - 1], i - 1, j - 1))
            prev_cost, pi, pj = min(candidates, key=lambda x: x[0])
            acc[i, j] = cost[i, j] + prev_cost
            back[i, j] = (pi, pj)

    i, j = n - 1, m - 1
    path = [(i, j)]
    while i != 0 or j != 0:
        i, j = map(int, back[i, j])
        path.append((i, j))
    path.reverse()
    return float(acc[n - 1, m - 1] / max(1, len(path))), path


def path_map(path: list[tuple[int, int]], perf_frames: int) -> np.ndarray:
    buckets: list[list[int]] = [[] for _ in range(perf_frames)]
    for i, j in path:
        if 0 <= i < perf_frames:
            buckets[i].append(j)

    out = np.zeros(perf_frames, dtype=np.int32)
    last = 0
    for i, values in enumerate(buckets):
        if values:
            last = int(round(sum(values) / len(values)))
        out[i] = last
    return out


def reference_pitch_class_cents(ref_chroma: np.ndarray) -> np.ndarray:
    # 0=C, 900=A, etc. Absolute octave is intentionally not inferred here.
    pc = np.argmax(ref_chroma, axis=0)
    return pc.astype(np.float32) * 100.0


def pc_error(cents_vs_a440: float, target_pc_cents_c0: float) -> float:
    # VX debug cents are relative to A440. Convert to C-based pitch class.
    c_based = cents_vs_a440 + 900.0
    error = (c_based - target_pc_cents_c0 + 600.0) % 1200.0 - 600.0
    return error


def median(values: list[float]) -> float:
    if not values:
        return 0.0
    return float(np.median(np.asarray(values, dtype=np.float64)))


def choose_reference(
    perf_stem: str, perf_chroma: np.ndarray, ref_by_pattern: dict[str, list[Path]]
) -> tuple[str, float, np.ndarray, np.ndarray]:
    pattern = stem_pattern(perf_stem)
    refs = ref_by_pattern.get(pattern, [])
    if not refs:
        raise FileNotFoundError(f"No reference chroma for {pattern}")

    best_ref = ""
    best_cost = math.inf
    best_map: np.ndarray | None = None
    best_ref_pc: np.ndarray | None = None
    for ref_path in refs:
        ref_chroma = load_chroma_12(ref_path)
        cost = cosine_cost_matrix(perf_chroma, ref_chroma)
        norm_cost, path = dtw_path(cost)
        if norm_cost < best_cost:
            best_cost = norm_cost
            best_ref = ref_path.stem.replace(".chroma", "")
            best_map = path_map(path, perf_chroma.shape[1])
            best_ref_pc = reference_pitch_class_cents(ref_chroma)

    assert best_map is not None and best_ref_pc is not None
    return best_ref, best_cost, best_map, best_ref_pc


def analyse_render(
    row: dict[str, str],
    chroma_dir: Path,
    ref_by_pattern: dict[str, list[Path]],
    alignment_cache: dict[str, tuple[str, float, np.ndarray, np.ndarray]],
) -> dict[str, object]:
    perf_stem = row["id"]
    debug_rows = load_csv(Path(row["debug_csv"]))
    perf_chroma = load_chroma_12(chroma_path(chroma_dir, perf_stem))
    if perf_stem not in alignment_cache:
        alignment_cache[perf_stem] = choose_reference(perf_stem, perf_chroma, ref_by_pattern)
    ref_stem, align_cost, perf_to_ref, ref_pc = alignment_cache[perf_stem]

    duration = max((f(r["seconds"]) for r in debug_rows), default=1.0)
    if duration <= 0.0:
        duration = 1.0

    stable_input: list[float] = []
    stable_output: list[float] = []
    stable_improvements: list[float] = []
    attacks_total = 0
    attacks_corrected = 0
    low_conf_total = 0
    low_conf_corrected = 0
    ambiguous_target_frames = 0
    large_wrong_target_frames = 0

    prev_target: float | None = None
    target_changes = 0

    for debug in debug_rows:
        seconds = f(debug["seconds"])
        perf_index = int(np.clip(round(seconds / duration * (perf_chroma.shape[1] - 1)),
                                 0, perf_chroma.shape[1] - 1))
        ref_index = int(np.clip(perf_to_ref[perf_index], 0, len(ref_pc) - 1))
        target = float(ref_pc[ref_index])
        if prev_target is not None and abs(((target - prev_target + 600.0) % 1200.0) - 600.0) >= 50:
            target_changes += 1
        prev_target = target

        confidence = f(debug["confidence"])
        residual = f(debug["residual_cents"])
        correction = f(debug["correction_cents"])
        detected = f(debug["centre_cents"]) + residual
        corrected = f(debug["corrected_cents"])

        input_error = abs(pc_error(detected, target))
        output_error = abs(pc_error(corrected, target))
        abs_correction = abs(correction)

        if confidence >= 0.75 and abs(residual) < 35.0:
            stable_input.append(input_error)
            stable_output.append(output_error)
            stable_improvements.append(input_error - output_error)
            if input_error < 20.0 and abs_correction > 8.0 and output_error > input_error + 5.0:
                large_wrong_target_frames += 1

        if int(f(debug["reason"])) == 4 or abs(residual) > 45.0:
            attacks_total += 1
            if abs_correction > 5.0:
                attacks_corrected += 1

        if confidence < 0.55:
            low_conf_total += 1
            if abs_correction > 5.0:
                low_conf_corrected += 1

        if confidence >= 0.65 and 35.0 <= input_error <= 65.0:
            ambiguous_target_frames += 1

    stable_count = len(stable_input)
    harm_frames = sum(1 for v in stable_improvements if v < -5.0)

    def ratio(a: int, b: int) -> float:
        return a / b if b else 0.0

    labels: list[str] = []
    if stable_count and median(stable_improvements) < -2.0:
        labels.append("stable_pitch_harmed")
    if ratio(attacks_corrected, attacks_total) > 0.35:
        labels.append("attack_or_transition_intervention")
    if ratio(low_conf_corrected, low_conf_total) > 0.25:
        labels.append("low_confidence_intervention")
    if stable_count and harm_frames / stable_count > 0.25:
        labels.append("many_harmed_stable_frames")
    if stable_count and large_wrong_target_frames / stable_count > 0.10:
        labels.append("likely_wrong_target_when_already_close")
    if ambiguous_target_frames > 0.25 * max(1, len(debug_rows)):
        labels.append("ambiguous_between_targets")
    if not labels:
        labels.append("review")

    return {
        "id": row["id"],
        "preset": row["preset"],
        "score": row["score"],
        "group": row["group"],
        "reference_id": ref_stem,
        "alignment_cost": f"{align_cost:.4f}",
        "reference_target_changes": target_changes,
        "stable_frames": stable_count,
        "median_ref_input_error_cents": f"{median(stable_input):.2f}",
        "median_ref_output_error_cents": f"{median(stable_output):.2f}",
        "median_ref_improvement_cents": f"{median(stable_improvements):.2f}",
        "harmed_stable_frame_ratio": f"{ratio(harm_frames, stable_count):.3f}",
        "attack_corrected_ratio": f"{ratio(attacks_corrected, attacks_total):.3f}",
        "low_confidence_corrected_ratio": f"{ratio(low_conf_corrected, low_conf_total):.3f}",
        "ambiguous_target_frames": ambiguous_target_frames,
        "likely_wrong_target_frames": large_wrong_target_frames,
        "labels": ";".join(labels),
        "wet_path": row["wet_path"],
        "debug_csv": row["debug_csv"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("batch_dir", type=Path)
    parser.add_argument("--mast-root", type=Path, default=Path("data/vxtune/mast"))
    args = parser.parse_args()

    chroma_dir = args.mast_root / "chroma" / "MAST_melody_chroma"
    ref_by_pattern: dict[str, list[Path]] = defaultdict(list)
    for ref_path in sorted(chroma_dir.glob("*_ref*.chroma.npy")):
        ref_by_pattern[stem_pattern(ref_path.stem.replace(".chroma", ""))].append(ref_path)

    metrics = load_csv(args.batch_dir / "vxtune_batch_metrics.csv")
    rows = []
    alignment_cache: dict[str, tuple[str, float, np.ndarray, np.ndarray]] = {}
    for row in metrics:
        if row.get("status") != "ok":
            continue
        rows.append(analyse_render(row, chroma_dir, ref_by_pattern, alignment_cache))

    fieldnames = [
        "id",
        "preset",
        "score",
        "group",
        "reference_id",
        "alignment_cost",
        "reference_target_changes",
        "stable_frames",
        "median_ref_input_error_cents",
        "median_ref_output_error_cents",
        "median_ref_improvement_cents",
        "harmed_stable_frame_ratio",
        "attack_corrected_ratio",
        "low_confidence_corrected_ratio",
        "ambiguous_target_frames",
        "likely_wrong_target_frames",
        "labels",
        "wet_path",
        "debug_csv",
    ]
    out_csv = args.batch_dir / "vxtune_musicality_metrics.csv"
    write_csv(out_csv, rows, fieldnames)

    by_label: defaultdict[str, int] = defaultdict(int)
    for row in rows:
        for label in str(row["labels"]).split(";"):
            by_label[label] += 1

    summary = args.batch_dir / "vxtune_musicality_summary.md"
    worst = sorted(rows, key=lambda r: float(r["median_ref_improvement_cents"]))[:15]
    summary.write_text(
        "\n".join(
            [
                "# VX Tune MAST Musicality Summary",
                "",
                f"Rows: {len(rows)}",
                "",
                "## Label Counts",
                "",
                *[f"- {label}: {count}" for label, count in sorted(by_label.items())],
                "",
                "## Worst Reference-Aligned Improvements",
                "",
                "| ID | Preset | Score | Ref | Improvement | Labels |",
                "| --- | --- | --- | --- | --- | --- |",
                *[
                    f"| {r['id']} | {r['preset']} | {r['score']} | {r['reference_id']} | "
                    f"{r['median_ref_improvement_cents']}c | {r['labels']} |"
                    for r in worst
                ],
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"Wrote {out_csv}")
    print(f"Wrote {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
