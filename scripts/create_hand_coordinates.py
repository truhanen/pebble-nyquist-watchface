#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ImportError as exc:
    raise SystemExit(
        "matplotlib is required. Run with: uv run python scripts/create_hand_coordinates.py"
    ) from exc


TRIG_MAX_ANGLE = 0x10000
TRIG_MAX_RATIO = 0x10000
SHIFTS = (
    (0, 0),
    (1, 0),
    (-1, 0),
    (0, 1),
    (0, -1),
    (1, 1),
    (1, -1),
    (-1, 1),
    (-1, -1),
)


def c_div(n: int, d: int) -> int:
    q = abs(n) // abs(d)
    return -q if (n < 0) ^ (d < 0) else q


def c_mod(n: int, d: int) -> int:
    return n - c_div(n, d) * d


def trig_lookup(angle: int) -> tuple[int, int]:
    radians = angle * (2.0 * math.pi / TRIG_MAX_ANGLE)
    sin_a = int(round(math.sin(radians) * TRIG_MAX_RATIO))
    cos_a = int(round(math.cos(radians) * TRIG_MAX_RATIO))
    return sin_a, cos_a


def trig_offset_trunc(value: int, trig: int) -> int:
    return c_div(value * trig, TRIG_MAX_RATIO)


def trig_offset_compensated(value: int, trig: int) -> int:
    scaled = value * trig
    base = c_div(scaled, TRIG_MAX_RATIO)
    if c_mod(scaled, TRIG_MAX_RATIO) != 0:
        base += 1 if scaled > 0 else -1
    return base


def abs64(v: int) -> int:
    return -v if v < 0 else v


def corner_radius_error(
    center: tuple[int, int],
    outer_left: tuple[int, int],
    outer_right: tuple[int, int],
    target_r2: int,
) -> int:
    ldx = outer_left[0] - center[0]
    ldy = outer_left[1] - center[1]
    rdx = outer_right[0] - center[0]
    rdy = outer_right[1] - center[1]
    left_r2 = ldx * ldx + ldy * ldy
    right_r2 = rdx * rdx + rdy * rdy
    return abs64(left_r2 - target_r2) + abs64(right_r2 - target_r2)


def isqrt32(n: int) -> int:
    if n <= 0:
        return 0
    x = n
    y = (x + 1) // 2
    while y < x:
        x = y
        y = (x + n // x) // 2
    return x


def tick_inner(outer: tuple[int, int], center: tuple[int, int], tick_len: int) -> tuple[int, int]:
    dx = center[0] - outer[0]
    dy = center[1] - outer[1]
    dist = isqrt32(dx * dx + dy * dy)
    if dist == 0:
        return outer
    return (
        outer[0] + c_div(dx * tick_len, dist),
        outer[1] + c_div(dy * tick_len, dist),
    )


def platform_canvas_geometry(platform: str) -> tuple[int, int, tuple[int, int], int, int]:
    if platform == "emery":
        w, h = 200, 228
        center = (w // 2, 110)
        tick_r = 85
        tick_len = 6
    else:
        w, h = 260, 260
        center = (w // 2, h // 2)
        half = min(w, h) // 2
        tick_r = (half * 84 + 50) // 100
        tick_len = 8

    return w, h, center, tick_r, tick_len


def scale_by_tick_radius(value: int, src_tick_r: int, dst_tick_r: int) -> int:
    return int(math.floor((value * dst_tick_r / src_tick_r) + 0.5))


def platform_hand_defaults(platform: str) -> tuple[int, int, int, int, int, int, int]:
    emery_defaults = (
        18,  # hand_tail
        86,  # minute_outer_dist
        15,  # minute_width
        6,   # minute_apex_ext
        54,  # hour_outer_dist
        25,  # hour_width
        10,  # hour_apex_ext
    )
    if platform == "emery":
        return emery_defaults

    _, _, _, emery_tick_r, _ = platform_canvas_geometry("emery")
    _, _, _, gabbro_tick_r, _ = platform_canvas_geometry("gabbro")
    return (
        scale_by_tick_radius(emery_defaults[0], emery_tick_r, gabbro_tick_r),
        scale_by_tick_radius(emery_defaults[1], emery_tick_r, gabbro_tick_r),
        scale_by_tick_radius(emery_defaults[2], emery_tick_r, gabbro_tick_r),
        scale_by_tick_radius(emery_defaults[3], emery_tick_r, gabbro_tick_r),
        scale_by_tick_radius(emery_defaults[4], emery_tick_r, gabbro_tick_r),
        scale_by_tick_radius(emery_defaults[5], emery_tick_r, gabbro_tick_r),
        scale_by_tick_radius(emery_defaults[6], emery_tick_r, gabbro_tick_r),
    )


def platform_stroke_defaults(platform: str) -> tuple[int, int]:
    emery_defaults = (9, 2)  # hand_edge_w, hand_halo_w
    if platform == "emery":
        return emery_defaults
    _, _, _, emery_tick_r, _ = platform_canvas_geometry("emery")
    _, _, _, gabbro_tick_r, _ = platform_canvas_geometry("gabbro")
    return (
        scale_by_tick_radius(emery_defaults[0], emery_tick_r, gabbro_tick_r),
        2,
    )


def axis_ticks(min_v: int, max_v: int, step: int = 20) -> list[int]:
    start = step * math.floor(min_v / step)
    end = step * math.ceil(max_v / step)
    return list(range(start, end + 1, step))


def rel_point(p: tuple[float, float], center: tuple[int, int]) -> tuple[float, float]:
    return p[0] - center[0], p[1] - center[1]


def rel_points(
    pts: dict[str, tuple[float, float]], center: tuple[int, int]
) -> dict[str, tuple[float, float]]:
    return (
        {k: rel_point(v, center) for k, v in pts.items()}
    )


def draw_hand_border_points(
    center: tuple[int, int],
    angle: int,
    outer_dist: int,
    half_width: int,
    apex_ext: int,
    tail: int,
    use_radial_tiebreak: bool = True,
) -> dict[str, tuple[int, int]]:
    sin_a, cos_a = trig_lookup(angle)

    inner_pt = (
        center[0] - c_div(tail * sin_a, TRIG_MAX_RATIO),
        center[1] + c_div(tail * cos_a, TRIG_MAX_RATIO),
    )
    outer_pt = (
        center[0] + trig_offset_compensated(outer_dist, sin_a),
        center[1] - trig_offset_compensated(outer_dist, cos_a),
    )
    ideal_outer_pt = outer_pt

    x_diff_left = trig_offset_trunc(half_width, cos_a)
    y_diff_left = trig_offset_trunc(half_width, sin_a)
    x_diff_right = trig_offset_compensated(half_width, cos_a)
    y_diff_right = trig_offset_compensated(half_width, sin_a)
    ideal_outer_right = (ideal_outer_pt[0] + x_diff_right, ideal_outer_pt[1] + y_diff_right)
    ideal_outer_left = (ideal_outer_pt[0] - x_diff_left, ideal_outer_pt[1] - y_diff_left)
    ideal_left_dx = ideal_outer_left[0] - center[0]
    ideal_left_dy = ideal_outer_left[1] - center[1]
    ideal_right_dx = ideal_outer_right[0] - center[0]
    ideal_right_dy = ideal_outer_right[1] - center[1]
    ideal_left_r2 = ideal_left_dx * ideal_left_dx + ideal_left_dy * ideal_left_dy
    ideal_right_r2 = ideal_right_dx * ideal_right_dx + ideal_right_dy * ideal_right_dy
    target_corner_r2 = (ideal_left_r2 + ideal_right_r2) // 2

    inner_right = (inner_pt[0] + x_diff_right, inner_pt[1] + y_diff_right)
    inner_left = (inner_pt[0] - x_diff_left, inner_pt[1] - y_diff_left)
    inner_mid_x = (inner_left[0] + inner_right[0]) // 2
    inner_mid_y = (inner_left[1] + inner_right[1]) // 2

    # Stage 1
    best_i = 0
    best_error = -1
    for i, (sx, sy) in enumerate(SHIFTS):
        axis_dx = (outer_pt[0] + sx) - inner_mid_x
        axis_dy = (outer_pt[1] + sy) - inner_mid_y
        cross = sin_a * axis_dy + cos_a * axis_dx
        err = abs64(cross)
        if best_error < 0 or err < best_error:
            best_error = err
            best_i = i
    if best_i != 0:
        sx, sy = SHIFTS[best_i]
        outer_pt = (outer_pt[0] + sx, outer_pt[1] + sy)

    outer_right = (outer_pt[0] + x_diff_right, outer_pt[1] + y_diff_right)
    outer_left = (outer_pt[0] - x_diff_left, outer_pt[1] - y_diff_left)
    apex = (
        outer_pt[0] + trig_offset_compensated(apex_ext, sin_a),
        outer_pt[1] - trig_offset_compensated(apex_ext, cos_a),
    )

    # Stage 1.5
    target_mid_x2 = 2 * outer_pt[0]
    target_mid_y2 = 2 * outer_pt[1]
    best_i = 0
    best_error = -1
    best_radial_error = -1
    for i, (sx, sy) in enumerate(SHIFTS):
        test_x = outer_right[0] + sx
        test_y = outer_right[1] + sy
        mid_x2 = outer_left[0] + test_x
        mid_y2 = outer_left[1] + test_y
        err = abs64(mid_x2 - target_mid_x2) + abs64(mid_y2 - target_mid_y2)
        radial_err = corner_radius_error(
            center,
            outer_left,
            (test_x, test_y),
            target_corner_r2,
        )
        if (
            best_error < 0
            or err < best_error
            or (use_radial_tiebreak and err == best_error and (best_radial_error < 0 or radial_err < best_radial_error))
        ):
            best_error = err
            best_radial_error = radial_err
            best_i = i
    if best_i != 0:
        sx, sy = SHIFTS[best_i]
        outer_right = (outer_right[0] + sx, outer_right[1] + sy)

    # Stage 2
    axis_dx = apex[0] - inner_mid_x
    axis_dy = apex[1] - inner_mid_y
    if axis_dx != 0 or axis_dy != 0:
        best_i = 0
        best_error = -1
        best_radial_error = -1
        for i, (sx, sy) in enumerate(SHIFTS):
            test_outer_mid_x = (outer_left[0] + sx + outer_right[0] + sx) // 2
            test_outer_mid_y = (outer_left[1] + sy + outer_right[1] + sy) // 2
            cross = axis_dx * (test_outer_mid_y - inner_mid_y) - axis_dy * (
                test_outer_mid_x - inner_mid_x
            )
            err = abs64(cross)
            radial_err = corner_radius_error(
                center,
                (outer_left[0] + sx, outer_left[1] + sy),
                (outer_right[0] + sx, outer_right[1] + sy),
                target_corner_r2,
            )
            if (
                best_error < 0
                or err < best_error
                or (use_radial_tiebreak and err == best_error and (best_radial_error < 0 or radial_err < best_radial_error))
            ):
                best_error = err
                best_radial_error = radial_err
                best_i = i
        if best_i != 0:
            sx, sy = SHIFTS[best_i]
            outer_left = (outer_left[0] + sx, outer_left[1] + sy)
            outer_right = (outer_right[0] + sx, outer_right[1] + sy)

    return {
        "inner_left": inner_left,
        "inner_right": inner_right,
        "outer_left": outer_left,
        "outer_right": outer_right,
        "apex": apex,
    }


def tick_segment(center: tuple[int, int], angle: int, tick_r: int, tick_len: int) -> tuple[tuple[int, int], tuple[int, int]]:
    sin_a, cos_a = trig_lookup(angle)
    outer = (
        center[0] + trig_offset_compensated(tick_r, sin_a),
        center[1] - trig_offset_compensated(tick_r, cos_a),
    )
    inner = tick_inner(outer, center, tick_len)

    best_i = 0
    best_error = -1
    for i, (sx, sy) in enumerate(SHIFTS):
        seg_dx = (outer[0] + sx) - (inner[0] + sx)
        seg_dy = (outer[1] + sy) - (inner[1] + sy)
        cross = sin_a * seg_dy + cos_a * seg_dx
        err = abs64(cross)
        if best_error < 0 or err < best_error:
            best_error = err
            best_i = i
    if best_i != 0:
        sx, sy = SHIFTS[best_i]
        outer = (outer[0] + sx, outer[1] + sy)
        inner = (inner[0] + sx, inner[1] + sy)

    return outer, inner


def perfect_tick_segment(
    center: tuple[int, int], angle: int, tick_r: int, tick_len: int
) -> tuple[tuple[float, float], tuple[float, float]]:
    radians = angle * (2.0 * math.pi / TRIG_MAX_ANGLE)
    sin_a = math.sin(radians)
    cos_a = math.cos(radians)
    outer_x = center[0] + tick_r * sin_a
    outer_y = center[1] - tick_r * cos_a
    inner_x = center[0] + (tick_r - tick_len) * sin_a
    inner_y = center[1] - (tick_r - tick_len) * cos_a
    return (outer_x, outer_y), (inner_x, inner_y)


def perfect_hand_border_points(
    center: tuple[int, int], angle: int, outer_dist: int, width: int, apex_ext: int, tail: int
) -> dict[str, tuple[float, float]]:
    radians = angle * (2.0 * math.pi / TRIG_MAX_ANGLE)
    sin_a = math.sin(radians)
    cos_a = math.cos(radians)

    inner_pt = (
        center[0] - tail * sin_a,
        center[1] + tail * cos_a,
    )
    outer_pt = (
        center[0] + outer_dist * sin_a,
        center[1] - outer_dist * cos_a,
    )
    half_width = width / 2.0
    x_diff = half_width * cos_a
    y_diff = half_width * sin_a

    return {
        "inner_left": (inner_pt[0] - x_diff, inner_pt[1] - y_diff),
        "inner_right": (inner_pt[0] + x_diff, inner_pt[1] + y_diff),
        "outer_left": (outer_pt[0] - x_diff, outer_pt[1] - y_diff),
        "outer_right": (outer_pt[0] + x_diff, outer_pt[1] + y_diff),
        "apex": (
            outer_pt[0] + apex_ext * sin_a,
            outer_pt[1] - apex_ext * cos_a,
        ),
    }


def point_distance(a: tuple[float, float], b: tuple[float, float]) -> float:
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return math.hypot(dx, dy)


def snap_to_nearest_pixel(v: float) -> int:
    if v >= 0:
        return int(math.floor(v + 0.5))
    return int(math.ceil(v - 0.5))


def snap_hand_points(perfect_pts: dict[str, tuple[float, float]]) -> dict[str, tuple[int, int]]:
    return {
        k: (snap_to_nearest_pixel(p[0]), snap_to_nearest_pixel(p[1]))
        for k, p in perfect_pts.items()
    }


def c_point_init(name: str, p: tuple[int, int], origin: tuple[int, int]) -> str:
    rel_x = p[0] - origin[0]
    rel_y = p[1] - origin[1]
    return f".{name} = {{ .x = {rel_x}, .y = {rel_y} }}"


def c_hand_pose_init(points: dict[str, tuple[int, int]], origin: tuple[int, int]) -> str:
    fields = ", ".join(
        [
            c_point_init("inner_left", points["inner_left"], origin),
            c_point_init("inner_right", points["inner_right"], origin),
            c_point_init("outer_left", points["outer_left"], origin),
            c_point_init("outer_right", points["outer_right"], origin),
            c_point_init("apex", points["apex"], origin),
        ]
    )
    return f"{{ {fields} }}"


def generate_hand_pose_series(
    center: tuple[int, int],
    hand_tail: int,
    minute_outer_dist: int,
    minute_width: int,
    minute_apex_ext: int,
    hour_outer_dist: int,
    hour_width: int,
    hour_apex_ext: int,
    hour_count: int,
) -> tuple[list[dict[str, tuple[int, int]]], list[dict[str, tuple[int, int]]]]:
    minute_results: list[dict[str, tuple[int, int]]] = []
    for minute in range(8):
        angle = minute * TRIG_MAX_ANGLE // 60
        perfect_pts = perfect_hand_border_points(
            center, angle, minute_outer_dist, minute_width, minute_apex_ext, hand_tail
        )
        minute_results.append(snap_hand_points(perfect_pts))

    hour_results: list[dict[str, tuple[int, int]]] = []
    for total_min in range(hour_count):
        hour = total_min // 60
        minute = total_min % 60
        angle = (hour % 12) * TRIG_MAX_ANGLE // 12 + minute * TRIG_MAX_ANGLE // 720
        perfect_pts = perfect_hand_border_points(
            center, angle, hour_outer_dist, hour_width, hour_apex_ext, hand_tail
        )
        hour_results.append(snap_hand_points(perfect_pts))

    return minute_results, hour_results


def emit_platform_pose_arrays(
    platform: str,
    minute_results: list[dict[str, tuple[int, int]]],
    hour_results: list[dict[str, tuple[int, int]]],
    center: tuple[int, int],
) -> None:
    print(
        f"// Pre-calculated snapped minute/hour poses for {platform.capitalize()} "
        "(center-relative)."
    )
    print(f"static const HandPose s_minute_base_00_to_07_{platform}[8] = {{")
    for points in minute_results:
        print(f"  {c_hand_pose_init(points, center)},")
    print("};")
    print()
    hour_sample_step = 5
    sampled_indices = list(range(0, len(hour_results), hour_sample_step))
    print(
        f"static const HandPose s_hour_base_00_to_89_{platform}_5min"
        f"[{len(sampled_indices)}] = {{"
    )
    for idx in sampled_indices:
        range_start = max(0, idx - 2)
        range_end = min(len(hour_results) - 1, idx + 2)
        label = (
            f"{range_start // 60}:{range_start % 60:02d}"
            f"-{range_end // 60}:{range_end % 60:02d}"
        )
        print(f"  /* {label} */ {c_hand_pose_init(hour_results[idx], center)},")
    print("};")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compute and visualize minute hand corner points."
    )
    parser.add_argument(
        "--platform",
        choices=("emery", "gabbro"),
        default="emery",
        help="Platform whose historical hand dimensions are used as defaults.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output PNG path (default: scripts/minute_hand_corners_subplots_0000_0007_<platform>.png).",
    )
    parser.add_argument("--hand-tail", type=int, default=None, help="Hand tail length.")
    parser.add_argument("--minute-outer-dist", type=int, default=None, help="Minute hand outer distance.")
    parser.add_argument("--minute-width", type=int, default=None, help="Minute hand width.")
    parser.add_argument("--minute-apex-ext", type=int, default=None, help="Minute hand apex extension.")
    parser.add_argument("--hour-outer-dist", type=int, default=None, help="Hour hand outer distance.")
    parser.add_argument("--hour-width", type=int, default=None, help="Hour hand width.")
    parser.add_argument("--hour-apex-ext", type=int, default=None, help="Hour hand apex extension.")
    args = parser.parse_args()

    w, h, center, tick_r, tick_len = platform_canvas_geometry(args.platform)
    (
        hand_tail,
        minute_outer_dist,
        minute_width,
        minute_apex_ext,
        hour_outer_dist,
        hour_width,
        hour_apex_ext,
    ) = platform_hand_defaults(args.platform)
    _hand_edge_w, _hand_halo_w = platform_stroke_defaults(args.platform)
    if args.hand_tail is not None:
        hand_tail = args.hand_tail
    if args.minute_outer_dist is not None:
        minute_outer_dist = args.minute_outer_dist
    if args.minute_width is not None:
        minute_width = args.minute_width
    if args.minute_apex_ext is not None:
        minute_apex_ext = args.minute_apex_ext
    if args.hour_outer_dist is not None:
        hour_outer_dist = args.hour_outer_dist
    if args.hour_width is not None:
        hour_width = args.hour_width
    if args.hour_apex_ext is not None:
        hour_apex_ext = args.hour_apex_ext
    repo_root = Path(__file__).resolve().parent.parent
    output = (
        args.output
        if args.output is not None
        else repo_root
        / "tmp"
        / f"minute_hand_corners_subplots_0000_0007_{args.platform}.png"
    )
    hour_output = (
        output.parent / f"hour_hand_subplots_0000_0125_{args.platform}.png"
    )
    output.parent.mkdir(parents=True, exist_ok=True)

    overrides = {
        "hand_tail": args.hand_tail,
        "minute_outer_dist": args.minute_outer_dist,
        "minute_width": args.minute_width,
        "minute_apex_ext": args.minute_apex_ext,
        "hour_outer_dist": args.hour_outer_dist,
        "hour_width": args.hour_width,
        "hour_apex_ext": args.hour_apex_ext,
    }

    for idx, platform in enumerate(("emery", "gabbro")):
        _, _, c_platform, _, _ = platform_canvas_geometry(platform)
        (
            p_hand_tail,
            p_minute_outer_dist,
            p_minute_width,
            p_minute_apex_ext,
            p_hour_outer_dist,
            p_hour_width,
            p_hour_apex_ext,
        ) = platform_hand_defaults(platform)
        if platform == args.platform:
            if overrides["hand_tail"] is not None:
                p_hand_tail = overrides["hand_tail"]
            if overrides["minute_outer_dist"] is not None:
                p_minute_outer_dist = overrides["minute_outer_dist"]
            if overrides["minute_width"] is not None:
                p_minute_width = overrides["minute_width"]
            if overrides["minute_apex_ext"] is not None:
                p_minute_apex_ext = overrides["minute_apex_ext"]
            if overrides["hour_outer_dist"] is not None:
                p_hour_outer_dist = overrides["hour_outer_dist"]
            if overrides["hour_width"] is not None:
                p_hour_width = overrides["hour_width"]
            if overrides["hour_apex_ext"] is not None:
                p_hour_apex_ext = overrides["hour_apex_ext"]
        p_minute_results, p_hour_results = generate_hand_pose_series(
            c_platform,
            p_hand_tail,
            p_minute_outer_dist,
            p_minute_width,
            p_minute_apex_ext,
            p_hour_outer_dist,
            p_hour_width,
            p_hour_apex_ext,
            hour_count=90,
        )
        if idx > 0:
            print()
        emit_platform_pose_arrays(platform, p_minute_results, p_hour_results, c_platform)

    minute_points_only, hour_points_only = generate_hand_pose_series(
        center,
        hand_tail,
        minute_outer_dist,
        minute_width,
        minute_apex_ext,
        hour_outer_dist,
        hour_width,
        hour_apex_ext,
        hour_count=86,
    )
    results = list(enumerate(minute_points_only))
    hour_results = list(enumerate(hour_points_only))

    colors = [
        (68, 1, 84),
        (72, 36, 117),
        (64, 67, 135),
        (52, 94, 141),
        (41, 120, 142),
        (32, 144, 140),
        (34, 167, 132),
        (68, 190, 112),
    ]
    colors_norm = [(r / 255.0, g / 255.0, b / 255.0) for (r, g, b) in colors]
    tick_color = (40 / 255.0, 40 / 255.0, 40 / 255.0)

    fig, axes = plt.subplots(2, 4, figsize=(24, 12), constrained_layout=True)
    axes_flat = axes.flatten()
    x_min = -center[0]
    x_max = w - center[0]
    y_min = -center[1]
    y_max = h - center[1]
    x_ticks = axis_ticks(x_min, x_max, 20)
    y_ticks = axis_ticks(y_min, y_max, 20)

    for ax, (minute, pts), color in zip(axes_flat, results, colors_norm):
        angle = minute * TRIG_MAX_ANGLE // 60
        perfect_pts = perfect_hand_border_points(
            center, angle, minute_outer_dist, minute_width, minute_apex_ext, hand_tail
        )
        pts_rel = rel_points(pts, center)
        perfect_rel = rel_points(perfect_pts, center)

        for tick_minute in range(0, 9):
            tick_angle = tick_minute * TRIG_MAX_ANGLE // 60
            tick_outer, tick_inner_pt = perfect_tick_segment(center, tick_angle, tick_r, tick_len)
            tick_outer_rel = rel_point(tick_outer, center)
            tick_inner_rel = rel_point(tick_inner_pt, center)
            ax.plot(
                [tick_outer_rel[0], tick_inner_rel[0]],
                [tick_outer_rel[1], tick_inner_rel[1]],
                color=tick_color,
                linewidth=1,
            )

        polygon = [
            pts_rel["inner_left"],
            pts_rel["inner_right"],
            pts_rel["outer_right"],
            pts_rel["apex"],
            pts_rel["outer_left"],
            pts_rel["inner_left"],
        ]
        ax.plot(
            [p[0] for p in polygon],
            [p[1] for p in polygon],
            color=color,
            linewidth=1,
        )
        perfect_polygon = [
            perfect_rel["inner_left"],
            perfect_rel["inner_right"],
            perfect_rel["outer_right"],
            perfect_rel["apex"],
            perfect_rel["outer_left"],
            perfect_rel["inner_left"],
        ]
        ax.plot(
            [p[0] for p in perfect_polygon],
            [p[1] for p in perfect_polygon],
            color=(0.75, 0.75, 0.75),
            linewidth=1,
        )
        ax.scatter(
            [pts_rel["outer_left"][0], pts_rel["outer_right"][0], pts_rel["apex"][0], 0],
            [pts_rel["outer_left"][1], pts_rel["outer_right"][1], pts_rel["apex"][1], 0],
            c=[color, color, color, (0, 0, 0)],
            s=[10, 10, 10, 10],
        )

        point_keys = ("inner_left", "inner_right", "outer_left", "outer_right", "apex")
        labels = {"inner_left": "IL", "inner_right": "IR", "outer_left": "OL", "outer_right": "OR", "apex": "AP"}
        distance_lines: list[str] = []
        for key in point_keys:
            dist = point_distance(perfect_pts[key], (float(pts[key][0]), float(pts[key][1])))
            distance_lines.append(f"{labels[key]}:{dist:.2f}px")
        ax.text(
            0.02,
            0.98,
            "\n".join(distance_lines),
            transform=ax.transAxes,
            va="top",
            ha="left",
            fontsize=8,
            family="monospace",
            bbox={"boxstyle": "round,pad=0.25", "facecolor": "white", "alpha": 0.8, "edgecolor": "#999"},
        )

        ax.set_title(f"00:{minute:02d}")
        ax.set_xlim(x_min, x_max)
        ax.set_ylim(y_max, y_min)
        ax.set_aspect("equal", adjustable="box")
        ax.set_xticks(x_ticks)
        ax.set_yticks(y_ticks)
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.grid(True, linestyle=":", linewidth=0.5, alpha=0.8)

    fig.suptitle(f"Minute hand geometry by minute (00:00-00:07, {args.platform})", fontsize=16)
    fig.savefig(output, dpi=180)
    print(f"\nSaved visualization: {output}", file=sys.stderr)

    # Hour hand figure: every minute from 00:00 to 01:25, grouped into 15-minute subplots.
    hour_totals = list(range(0, 86))
    group_starts = [0, 15, 30, 45, 60, 75]
    fig_h, axes_h = plt.subplots(2, 3, figsize=(24, 14), constrained_layout=True)
    axes_h_flat = axes_h.flatten()
    x_ticks = axis_ticks(x_min, x_max, 20)
    y_ticks = axis_ticks(y_min, y_max, 20)

    for ax, start in zip(axes_h_flat, group_starts):
        end = min(start + 14, hour_totals[-1])
        segment = list(range(start, end + 1))
        n = max(1, len(segment) - 1)

        # Perfect hour ticks as dial reference.
        for hour_idx in range(12):
            tick_angle = hour_idx * TRIG_MAX_ANGLE // 12
            tick_outer, tick_inner_pt = perfect_tick_segment(center, tick_angle, tick_r, tick_len)
            tick_outer_rel = rel_point(tick_outer, center)
            tick_inner_rel = rel_point(tick_inner_pt, center)
            ax.plot(
                [tick_outer_rel[0], tick_inner_rel[0]],
                [tick_outer_rel[1], tick_inner_rel[1]],
                color=(0.6, 0.6, 0.6),
                linewidth=1,
            )

        for idx, total_min in enumerate(segment):
            hour = total_min // 60
            minute = total_min % 60
            angle = (hour % 12) * TRIG_MAX_ANGLE // 12 + minute * TRIG_MAX_ANGLE // 720
            perfect_pts = perfect_hand_border_points(
                center, angle, hour_outer_dist, hour_width, hour_apex_ext, hand_tail
            )
            snapped_pts = snap_hand_points(perfect_pts)
            perfect_rel = rel_points(perfect_pts, center)
            snapped_rel = rel_points(snapped_pts, center)

            t = idx / n
            color = (0.2 + 0.6 * t, 0.2, 0.8 - 0.6 * t)

            snapped_polygon = [
                snapped_rel["inner_left"],
                snapped_rel["inner_right"],
                snapped_rel["outer_right"],
                snapped_rel["apex"],
                snapped_rel["outer_left"],
                snapped_rel["inner_left"],
            ]
            ax.plot(
                [p[0] for p in snapped_polygon],
                [p[1] for p in snapped_polygon],
                color=color,
                linewidth=1.2,
                alpha=0.9,
            )
            perfect_polygon = [
                perfect_rel["inner_left"],
                perfect_rel["inner_right"],
                perfect_rel["outer_right"],
                perfect_rel["apex"],
                perfect_rel["outer_left"],
                perfect_rel["inner_left"],
            ]
            ax.plot(
                [p[0] for p in perfect_polygon],
                [p[1] for p in perfect_polygon],
                color=(0.82, 0.82, 0.82),
                linewidth=1.2,
                alpha=0.7,
            )

        ax.scatter([0], [0], c=[(0, 0, 0)], s=[12])
        ax.set_title(f"{start//60:02d}:{start%60:02d} - {end//60:02d}:{end%60:02d}")
        ax.set_xlim(x_min, x_max)
        ax.set_ylim(y_max, y_min)
        ax.set_aspect("equal", adjustable="box")
        ax.set_xticks(x_ticks)
        ax.set_yticks(y_ticks)
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.grid(True, linestyle=":", linewidth=0.5, alpha=0.8)

    fig_h.suptitle(f"Hour hand geometry by 15-minute windows (00:00-01:25, {args.platform})", fontsize=16)
    fig_h.savefig(hour_output, dpi=180)
    print(f"Saved visualization: {hour_output}", file=sys.stderr)


if __name__ == "__main__":
    main()
