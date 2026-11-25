"""Cross-photo consistency of predictions, explicitly not labeled accuracy."""

from pathlib import Path

import cv2
import numpy as np

from .hex_grid import SIZE, make_grid
from .io_utils import read_image, write_json
from .terrain_recognition import TerrainRecognizer, extract_features


def terrain_consistency(output, calibration_cases):
    output = Path(output)
    reader = TerrainRecognizer()
    cases = []
    baseline = None
    empty = np.zeros((SIZE, SIZE), np.uint8)
    for case in calibration_cases:
        if case["status"] != "success":
            continue
        path = output / case["directory"]
        image = read_image(path / "calibrated.jpg")
        valid = cv2.imread(str(path / "valid_pixels.png"), cv2.IMREAD_GRAYSCALE)
        records = []
        for tile in make_grid():
            features, _ = extract_features(image, tile, empty, empty, valid)
            result = reader.recognize(features)
            records.append(
                {
                    "tile_id": tile["tile_id"],
                    "prediction": result["terrain"],
                    "hsv": {k: features[k] for k in ["h", "s", "v", "circular_mean_h"]},
                    "score": result["score"],
                }
            )
        if baseline is None:
            baseline = records
        agreed = sum(
            a["prediction"] == b["prediction"]
            for a, b in zip(records, baseline, strict=True)
        )
        cases.append(
            {
                "image": case["name"],
                "tiles_matching_first_image_predictions": agreed,
                "tile_count": 19,
                "tiles": records,
            }
        )
    result = {
        "metric": "Prediction agreement with the first bare-board image, not accuracy.",
        "assumptions": "These supplied bare-board photographs show the same physical tile arrangement.",
        "ground_truth_available": False,
        "cases": cases,
    }
    write_json(output / "terrain_consistency.json", result)
    return result
