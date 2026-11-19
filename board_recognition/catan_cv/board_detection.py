"""Blue sea-frame detection; this is a documented board-edition prior."""

import cv2
import numpy as np


class CalibrationError(ValueError):
    pass


def detect_boundary(image):
    scale = min(1.0, 1200 / max(image.shape[:2]))
    small = cv2.resize(image, None, fx=scale, fy=scale)
    hsv = cv2.cvtColor(cv2.GaussianBlur(small, (5, 5), 0), cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, np.array([85, 65, 45]), np.array([130, 255, 255]))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((11, 11), np.uint8))
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    candidates = []
    for contour in sorted(contours, key=cv2.contourArea, reverse=True)[:5]:
        hull = cv2.convexHull(contour)
        if cv2.contourArea(hull) < 0.1 * small.shape[0] * small.shape[1]:
            continue
        for epsilon in [0.015, 0.02, 0.025, 0.03, 0.04]:
            polygon = cv2.approxPolyDP(
                hull, epsilon * cv2.arcLength(hull, True), True
            ).reshape(-1, 2)
            if len(polygon) == 6:
                candidates.append((cv2.contourArea(hull), polygon))
                break
    if not candidates:
        raise CalibrationError(
            "No usable six-corner blue sea frame; provide manual correspondences or a reference image."
        )
    polygon = max(candidates, key=lambda item: item[0])[1].astype(np.float32)
    center = polygon.mean(axis=0)
    polygon = polygon[
        np.argsort(np.arctan2(polygon[:, 1] - center[1], polygon[:, 0] - center[0]))
    ]
    # Top-left reference corner; six rotations remain indistinguishable without a reference.
    angle = np.arctan2(polygon[:, 1] - center[1], polygon[:, 0] - center[0])
    start = np.argmin(np.abs(np.angle(np.exp(1j * (angle - np.deg2rad(-120))))))
    polygon = np.roll(polygon, -int(start), axis=0)
    clipped = bool(
        np.any(polygon[:, 0] <= 3)
        or np.any(polygon[:, 1] <= 3)
        or np.any(polygon[:, 0] >= small.shape[1] - 4)
        or np.any(polygon[:, 1] >= small.shape[0] - 4)
    )
    return (
        polygon / scale,
        {"boundary_clipped": clipped, "blue_frame_prior": True},
        mask,
    )
