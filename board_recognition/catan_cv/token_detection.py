"""Circle candidates validated by radial cream-color support."""

import cv2
import numpy as np

from .hex_grid import SIZE


def detect_token(image, tile):
    cx, cy = np.rint(tile["center"]).astype(int)
    extent = 67
    x0, y0 = cx - extent, cy - extent
    crop = image[y0 : cy + extent + 1, x0 : cx + extent + 1]
    hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
    circles = cv2.HoughCircles(
        cv2.GaussianBlur(gray, (5, 5), 1),
        cv2.HOUGH_GRADIENT,
        1,
        16,
        param1=90,
        param2=23,
        minRadius=17,
        maxRadius=29,
    )
    candidates = []
    yy, xx = np.indices(gray.shape)
    if circles is not None:
        for x, y, r in circles[0]:
            offset = float(np.hypot(x - extent, y - extent))
            if offset > 48:
                continue
            distance = np.hypot(xx - x, yy - y)
            interior = distance < r * 0.85
            cream = (
                (hsv[:, :, 0] >= 10)
                & (hsv[:, :, 0] <= 38)
                & (hsv[:, :, 1] < 185)
                & (hsv[:, :, 2] > 125)
            )
            support = float(np.mean(cream[interior]))
            if support < 0.62:
                continue
            score = support - 0.12 * offset / 48 - 0.08 * abs(r - 23) / 6
            candidates.append((score, float(x + x0), float(y + y0), float(r), support))
    exclusion = np.zeros((SIZE, SIZE), np.uint8)
    if not candidates:
        return (
            {"status": "not_detected", "center": None, "radius": None, "score": None},
            None,
            exclusion,
        )
    score, x, y, r, support = max(candidates)
    gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    angles = np.linspace(0, 2 * np.pi, 96, endpoint=False)
    dx, dy, dr = np.meshgrid(
        np.arange(-7, 8), np.arange(-7, 8), np.arange(20, 27), indexing="ij"
    )
    xs = x - x0 + dx.ravel()
    ys = y - y0 + dy.ravel()
    rs = dr.ravel()
    px = np.rint(xs[:, None] + rs[:, None] * np.cos(angles)).astype(int)
    py = np.rint(ys[:, None] + rs[:, None] * np.sin(angles)).astype(int)
    px = np.clip(px, 0, gray.shape[1] - 1)
    py = np.clip(py, 0, gray.shape[0] - 1)
    radial = np.abs(gx[py, px] * np.cos(angles) + gy[py, px] * np.sin(angles))
    ring_score = np.quantile(radial, 0.4, axis=1) + 0.3 * np.mean(
        np.minimum(radial, 400), axis=1
    )
    best = int(np.argmax(ring_score))
    x = float(xs[best] + x0)
    y = float(ys[best] + y0)
    r = float(rs[best])
    # Include the printed outer rim, interpolation, and a small localization margin.
    cv2.circle(exclusion, (round(x), round(y)), int(np.ceil(r + 5)), 255, -1)
    patch = cv2.getRectSubPix(image, (int(np.ceil(2.4 * r)),) * 2, (x, y))
    patch = cv2.resize(patch, (128, 128), interpolation=cv2.INTER_CUBIC)
    return (
        {
            "status": "detected",
            "center": [x, y],
            "radius": r,
            "score": float(score),
            "cream_support": support,
        },
        patch,
        exclusion,
    )


def possible_occlusion(image, tile, token, reference=None):
    """Flag compact neutral/dark foreground; optionally use a bare-board reference."""
    cx, cy = np.rint(tile["center"]).astype(int)
    yy, xx = np.indices(image.shape[:2])
    central = (xx - cx) ** 2 + (yy - cy) ** 2 < 48**2
    hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    candidate = central & (hsv[:, :, 1] < 150) & (hsv[:, :, 2] < 180)
    method = "central_neutral_dark_component"
    if reference is not None:
        # Normalize local lighting per channel before measuring appearance change.
        annulus = ((xx - cx) ** 2 + (yy - cy) ** 2 > 48**2) & (
            (xx - cx) ** 2 + (yy - cy) ** 2 < 65**2
        )
        current = image.astype(np.float32)
        baseline = reference.astype(np.float32)
        ratio = np.median(current[annulus], axis=0) / np.maximum(
            np.median(baseline[annulus], axis=0), 1
        )
        residual = np.linalg.norm(current - np.clip(baseline * ratio, 0, 255), axis=2)
        candidate &= residual > 55
        method = "reference_change_and_neutral_dark_component"
    mask = (candidate * 255).astype(np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    count, labels, stats, _ = cv2.connectedComponentsWithStats(mask)
    result = np.zeros_like(mask)
    for k in range(1, count):
        if stats[k, cv2.CC_STAT_AREA] >= 160:
            result[labels == k] = 255
    # A visible token may contain dark ink. This heuristic only flags missing circles.
    flagged = bool(np.count_nonzero(result) > 200 and token["status"] == "not_detected")
    if flagged:
        result = cv2.dilate(result, np.ones((11, 11), np.uint8))
        # Conservative center exclusion also removes the piece's cast shadow.
        cv2.circle(result, (cx, cy), 45, 255, -1)
    else:
        result[:] = 0
    return {
        "possible": flagged,
        "method": method,
        "status": "possible_occlusion" if flagged else "not_established",
    }, result
