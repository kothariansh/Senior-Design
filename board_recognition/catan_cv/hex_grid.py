import cv2
import numpy as np

SIZE = 1100
CENTER = np.array([550.0, 550.0])
RADIUS = 90.0
ROWS = (3, 4, 5, 4, 3)


def outer_vertices():
    angles = np.deg2rad([240, 300, 0, 60, 120, 180])
    return (CENTER + 500 * np.c_[np.cos(angles), np.sin(angles)]).astype(np.float32)


def make_grid():
    tiles = []
    angles = np.deg2rad(np.arange(6) * 60 - 90)
    for row, count in enumerate(ROWS):
        for col in range(count):
            center = CENTER + [
                (col - (count - 1) / 2) * np.sqrt(3) * RADIUS,
                (row - 2) * 1.5 * RADIUS,
            ]
            polygon = center + RADIUS * np.c_[np.cos(angles), np.sin(angles)]
            tiles.append(
                {
                    "tile_id": f"r{row}c{col}",
                    "row": row,
                    "col": col,
                    "center": center.tolist(),
                    "polygon": polygon.tolist(),
                }
            )
    return tiles


def tile_mask(tile, scale=0.79):
    center = np.array(tile["center"])
    polygon = center + scale * (np.array(tile["polygon"]) - center)
    mask = np.zeros((SIZE, SIZE), np.uint8)
    cv2.fillPoly(mask, [np.rint(polygon).astype(np.int32)], 255)
    return mask


def overlay(image, tiles=None):
    output = image.copy()
    for tile in tiles or make_grid():
        polygon = np.rint(tile["polygon"]).astype(np.int32)
        cv2.polylines(output, [polygon], True, (220, 40, 230), 2, cv2.LINE_AA)
        x, y = np.rint(tile["center"]).astype(int)
        cv2.putText(
            output,
            tile["tile_id"],
            (x - 24, y - 48),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
            (0, 0, 0),
            3,
            cv2.LINE_AA,
        )
        cv2.putText(
            output,
            tile["tile_id"],
            (x - 24, y - 48),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
            (255, 255, 255),
            1,
            cv2.LINE_AA,
        )
    return output
