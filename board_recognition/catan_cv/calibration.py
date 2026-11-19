"""Geometric calibration and classical SIFT reference registration."""

import cv2
import numpy as np

from .board_detection import CalibrationError, detect_boundary
from .hex_grid import SIZE, make_grid, outer_vertices


def fit_homography(source, target):
    source = np.asarray(source, np.float32)
    target = np.asarray(target, np.float32)
    if (
        source.shape != target.shape
        or source.ndim != 2
        or source.shape[1] != 2
        or len(source) < 4
    ):
        raise CalibrationError(
            "Provide at least four paired [x, y] source and canonical points."
        )
    if not np.isfinite(source).all() or not np.isfinite(target).all():
        raise CalibrationError("Reference coordinates must be finite.")
    for points in (source, target):
        if (
            len(np.unique(points, axis=0)) != len(points)
            or cv2.contourArea(cv2.convexHull(points)) < 10
        ):
            raise CalibrationError("Degenerate reference points.")
    H = (
        cv2.getPerspectiveTransform(source, target)
        if len(source) == 4
        else cv2.findHomography(source, target, 0)[0]
    )
    if (
        H is None
        or not np.isfinite(H).all()
        or abs(np.linalg.det(H)) < 1e-10
        or abs(H[2, 2]) < 1e-12
    ):
        raise CalibrationError("Singular homography.")
    H = H / H[2, 2]
    predicted = cv2.perspectiveTransform(source[None], H)[0]
    rms = float(np.sqrt(np.mean(np.sum((predicted - target) ** 2, axis=1))))
    return H, rms


def validate_transform(H, image_shape):
    inverse = np.linalg.inv(H)
    grid_points = np.float32([t["center"] for t in make_grid()])
    homogeneous = np.c_[grid_points, np.ones(len(grid_points))] @ inverse.T
    if np.min(homogeneous[:, 2]) * np.max(homogeneous[:, 2]) <= 0:
        raise CalibrationError("Homography horizon crosses the board.")
    points = homogeneous[:, :2] / homogeneous[:, 2:]
    height, width = image_shape[:2]
    visible = (
        (points[:, 0] >= 0)
        & (points[:, 0] < width)
        & (points[:, 1] >= 0)
        & (points[:, 1] < height)
    )
    if not visible.all():
        raise CalibrationError(
            "One or more expected tile centers lie outside the photograph."
        )
    area = cv2.contourArea(cv2.convexHull(points.astype(np.float32)))
    if area < 0.03 * height * width:
        raise CalibrationError("Projected board is implausibly small.")


def register_reference(image, reference, reference_H):
    def features(im):
        scale = min(1.0, 1600 / max(im.shape[:2]))
        small = cv2.resize(im, None, fx=scale, fy=scale)
        keypoints, desc = cv2.SIFT_create(nfeatures=6000).detectAndCompute(
            cv2.cvtColor(small, cv2.COLOR_BGR2GRAY), None
        )
        return keypoints, desc, scale

    key_a, desc_a, scale_a = features(image)
    key_b, desc_b, scale_b = features(reference)
    if desc_a is None or desc_b is None:
        raise CalibrationError("Insufficient reference features.")
    matches = cv2.BFMatcher().knnMatch(desc_a, desc_b, k=2)
    pairs = [
        (a.queryIdx, a.trainIdx) for a, b in matches if a.distance < 0.70 * b.distance
    ]
    if len(pairs) < 16:
        raise CalibrationError("Insufficient unambiguous reference matches.")
    src = np.float32([key_a[a].pt for a, b in pairs]) / scale_a
    ref = np.float32([key_b[b].pt for a, b in pairs]) / scale_b
    canonical = cv2.perspectiveTransform(ref[None], reference_H)[0]
    # Restrict matching to the board to avoid calibrating the background plane.
    inside = np.array(
        [
            cv2.pointPolygonTest(outer_vertices(), tuple(map(float, p)), False) >= 0
            for p in canonical
        ]
    )
    src, canonical = src[inside], canonical[inside]
    if len(src) < 16:
        raise CalibrationError("Insufficient on-board reference features.")
    cv2.setRNGSeed(27)
    H, inliers = cv2.findHomography(
        src, canonical, cv2.RANSAC, 3.0, maxIters=5000, confidence=0.999
    )
    if H is None or inliers is None:
        raise CalibrationError("Reference RANSAC failed.")
    keep = inliers.ravel().astype(bool)
    projected = cv2.perspectiveTransform(src[None], H)[0]
    error = np.linalg.norm(projected - canonical, axis=1)
    coverage = cv2.contourArea(cv2.convexHull(canonical[keep])) / cv2.contourArea(
        outer_vertices()
    )
    if (
        keep.sum() < 20
        or keep.mean() < 0.35
        or coverage < 0.25
        or np.median(error[keep]) > 2.5
    ):
        raise CalibrationError(
            "Reference registration failed inlier or spatial coverage checks."
        )
    validate_transform(H, image.shape)
    return H, {
        "reference_matches": len(src),
        "inliers": int(keep.sum()),
        "inlier_fraction": float(keep.mean()),
        "inlier_median_error_px": float(np.median(error[keep])),
        "inlier_rms_error_px": float(np.sqrt(np.mean(error[keep] ** 2))),
        "inlier_coverage": float(coverage),
        "error_interpretation": "Internal feature fit residual, not ground-truth alignment accuracy.",
    }


def calibrate(image, manual=None, reference=None, reference_H=None):
    diagnostics = {
        "method": "homography",
        "status": "success",
        "canonical_size": [SIZE, SIZE],
    }
    boundary = None
    mask = None
    try:
        boundary, details, mask = detect_boundary(image)
        diagnostics.update(details)
    except CalibrationError as exc:
        diagnostics["boundary_warning"] = str(exc)
    if manual is not None:
        H, rms = fit_homography(manual["source_points"], manual["canonical_points"])
        if rms > 12:
            raise CalibrationError(
                "Manual correspondences exceed the 12-pixel canonical fit tolerance."
            )
        diagnostics["manual_canonical_points"] = manual["canonical_points"]
        diagnostics.update(
            mode="manual", orientation="user_defined_correspondences", fit_rms_px=rms
        )
    elif reference is not None:
        try:
            H, details = register_reference(image, reference, reference_H)
            diagnostics.update(
                details, mode="automatic_reference", orientation="aligned_to_reference"
            )
        except CalibrationError as exc:
            diagnostics["reference_warning"] = str(exc)
            H = None
        if H is None:
            H, details = boundary_transform(boundary, diagnostics)
            diagnostics.update(details)
    else:
        H, details = boundary_transform(boundary, diagnostics)
        diagnostics.update(details)
    validate_transform(H, image.shape)
    diagnostics["homography"] = H.tolist()
    diagnostics["detected_boundary"] = (
        boundary.tolist() if boundary is not None else None
    )
    diagnostics["reference_points"] = (
        manual["source_points"]
        if manual is not None
        else diagnostics["detected_boundary"]
    )
    warped = cv2.warpPerspective(image, H, (SIZE, SIZE))
    valid = cv2.warpPerspective(
        np.full(image.shape[:2], 255, np.uint8),
        H,
        (SIZE, SIZE),
        flags=cv2.INTER_NEAREST,
    )
    return warped, H, diagnostics, valid, mask


def boundary_transform(boundary, diagnostics):
    if boundary is None:
        raise CalibrationError(
            diagnostics.get("boundary_warning", "No board boundary.")
        )
    if diagnostics.get("boundary_clipped"):
        raise CalibrationError(
            "Sea-frame corner is clipped; use reference registration or manual calibration."
        )
    H, rms = fit_homography(boundary, outer_vertices())
    if rms > 12:
        raise CalibrationError(
            f"Irregular frame: six-corner fit residual {rms:.1f} px exceeds 12 px."
        )
    return H, {
        "mode": "automatic_boundary",
        "orientation": "ambiguous_modulo_60_degrees",
        "fit_rms_px": rms,
        "error_interpretation": "Six-corner model fit residual, not independently measured alignment accuracy.",
    }
