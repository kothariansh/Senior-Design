"""Explainable representative-HSV matching, with explicit prototype provenance."""

import json
from pathlib import Path

import cv2
import numpy as np

from .hex_grid import tile_mask

RESOURCES = {
    "forest": "lumber",
    "pasture": "wool",
    "fields": "grain",
    "hills": "brick",
    "mountains": "ore",
    "desert": None,
}


def extract_features(image, tile, token_mask, occlusion_mask, valid_mask):
    mask = tile_mask(tile, 0.79)
    mask = cv2.bitwise_and(mask, valid_mask)
    mask[(token_mask > 0) | (occlusion_mask > 0)] = 0
    mask = cv2.erode(mask, np.ones((3, 3), np.uint8))
    hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    # Very dark pixels carry unreliable hue; clipping excludes only near-black/white extremes.
    mask[(hsv[:, :, 2] < 25) | ((hsv[:, :, 2] > 250) & (hsv[:, :, 1] < 15))] = 0
    pixels = hsv[mask > 0].astype(float)
    if len(pixels) < 500:
        return None, mask
    hue_pixels = pixels[pixels[:, 1] > 35]
    angles = hue_pixels[:, 0] * 2 * np.pi / 180
    weights = hue_pixels[:, 1] / 255
    z = np.sum(weights * np.exp(1j * angles)) / max(np.sum(weights), 1e-9)
    circular_hue = float((np.angle(z) * 180 / (2 * np.pi)) % 180)
    histogram = cv2.calcHist([hsv], [0, 1], mask, [18, 8], [0, 180, 0, 256]).astype(
        float
    )
    histogram /= histogram.sum()
    median = np.median(pixels, axis=0)
    return {
        "h": float(median[0]),
        "s": float(median[1]),
        "v": float(median[2]),
        "circular_mean_h": circular_hue,
        "hue_concentration": float(abs(z)),
        "mean_s": float(pixels[:, 1].mean()),
        "mean_v": float(pixels[:, 2].mean()),
        "std_v": float(pixels[:, 2].std()),
        "valid_pixel_count": len(pixels),
        "hsv_histogram_hs": histogram.ravel().tolist(),
        "histogram_shape": [18, 8],
        "low_saturation_fraction": float(np.mean(pixels[:, 1] < 90)),
        "green_hue_fraction": float(np.mean((pixels[:, 0] > 32) & (pixels[:, 0] < 90))),
    }, mask


class TerrainRecognizer:
    def __init__(self, path=None):
        path = (
            Path(path)
            if path
            else Path(__file__).resolve().parents[1] / "config/terrain_profiles.json"
        )
        self.config = json.loads(path.read_text())
        if set(self.config["profiles"]) != set(RESOURCES):
            raise ValueError("Terrain profiles must contain all six terrain classes.")
        scales = np.asarray(self.config["distance_scales"], float)
        if scales.shape != (3,) or not np.isfinite(scales).all() or (scales <= 0).any():
            raise ValueError("Distance scales must be three positive finite values.")
        self.scales = scales
        for name, values in self.config["profiles"].items():
            array = np.asarray(values, float)
            if array.ndim != 2 or array.shape[1] != 3 or not np.isfinite(array).all():
                raise ValueError(f"Invalid HSV reference profile: {name}")

    def recognize(self, features):
        if features is None:
            return {
                "terrain": None,
                "resource": None,
                "status": "insufficient_pixels",
                "score": None,
            }
        value = np.array([features["circular_mean_h"], features["s"], features["v"]])
        distances = {}
        for name, profiles in self.config["profiles"].items():
            delta = np.abs(np.array(profiles) - value)
            delta[:, 0] = np.minimum(delta[:, 0], 180 - delta[:, 0])
            distances[name] = float(np.min(np.linalg.norm(delta / self.scales, axis=1)))
        ranking = sorted(distances, key=distances.get)
        terrain = ranking[0]
        distance = distances[terrain]
        margin = distances[ranking[1]] - distance
        status = (
            "recognized"
            if self.config.get("verified_labels") and distance < 2 and margin > 0.2
            else "requires_review"
        )
        return {
            "terrain": terrain,
            "resource": RESOURCES[terrain],
            "score": float(np.exp(-(distance**2) / 2)),
            "status": status,
            "distance": distance,
            "margin": margin,
            "profile_provenance": self.config["provenance"],
            "candidates": [
                {"terrain": name, "distance": distances[name]} for name in ranking
            ],
        }
