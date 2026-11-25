"""Synthetic unit tests; these do not measure real-photo recognition accuracy."""

import json
import tempfile
import unittest
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from catan_cv.board_detection import CalibrationError, detect_boundary
from catan_cv.calibration import calibrate, fit_homography, validate_transform
from catan_cv.evaluation import read_verified
from catan_cv.hex_grid import SIZE, make_grid, outer_vertices
from catan_cv.number_recognition import PIPS, NumberRecognizer
from catan_cv.terrain_recognition import TerrainRecognizer, extract_features
from catan_cv.token_detection import detect_token, possible_occlusion
from catan_cv.validation import NUMBER_COUNTS, TERRAIN_COUNTS, validate_board

ROOT = Path(__file__).resolve().parents[1]


class GeometryTests(unittest.TestCase):
    def test_grid_topology(self):
        tiles = make_grid()
        self.assertEqual(len(tiles), 19)
        self.assertEqual(
            [sum(t["row"] == r for t in tiles) for r in range(5)], [3, 4, 5, 4, 3]
        )
        self.assertEqual(len({t["tile_id"] for t in tiles}), 19)
        for t in tiles:
            self.assertEqual(len(t["polygon"]), 6)
            self.assertGreater(
                cv2.pointPolygonTest(
                    np.float32(t["polygon"]), tuple(t["center"]), False
                ),
                0,
            )

    def test_four_point_perspective_roundtrip(self):
        src = np.float32([[20, 30], [600, 80], [580, 500], [60, 450]])
        dst = np.float32([[50, 50], [950, 50], [950, 950], [50, 950]])
        H, error = fit_homography(src, dst)
        self.assertLess(error, 0.001)
        points = np.float32([[[200, 200], [400, 300]]])
        actual = cv2.perspectiveTransform(
            cv2.perspectiveTransform(points, H), np.linalg.inv(H)
        )
        np.testing.assert_allclose(actual, points, atol=0.001)

    def test_six_point_known_projective_transform(self):
        truth = np.array([[1.2, 0.1, 50], [-0.05, 0.9, 40], [0.0002, -0.0001, 1.0]])
        src = cv2.perspectiveTransform(outer_vertices()[None], np.linalg.inv(truth))[0]
        actual, error = fit_homography(src, outer_vertices())
        np.testing.assert_allclose(actual, truth, atol=1e-4)
        self.assertLess(error, 0.001)

    def test_degenerate_correspondences_rejected(self):
        for points in [
            [[0, 0], [1, 1], [2, 2], [3, 3]],
            [[0, 0], [10, 0], [10, 10], [0, 0]],
        ]:
            with self.assertRaises(CalibrationError):
                fit_homography(points, points)

    def test_nonfinite_correspondences_rejected(self):
        with self.assertRaises(CalibrationError):
            fit_homography(
                [[0, 0], [10, 0], [10, 10], [0, np.nan]], outer_vertices()[:4]
            )

    def test_invalid_board_projection_rejected(self):
        with self.assertRaises(CalibrationError):
            validate_transform(
                np.array([[1.0, 0, -5000], [0, 1, 0], [0, 0, 1]]), (1000, 1000, 3)
            )

    def test_blank_image_rejected(self):
        with self.assertRaises(CalibrationError):
            detect_boundary(np.full((600, 800, 3), 200, np.uint8))

    def test_boundary_orientation_is_explicit(self):
        image = np.full((SIZE, SIZE, 3), 220, np.uint8)
        cv2.fillPoly(image, [outer_vertices().astype(np.int32)], (180, 80, 20))
        _, _, diagnostics, _, _ = calibrate(image)
        self.assertEqual(diagnostics["orientation"], "ambiguous_modulo_60_degrees")

    def test_manual_mode_uses_explicit_points(self):
        image = np.full((SIZE, SIZE, 3), 180, np.uint8)
        points = outer_vertices().tolist()
        _, H, diagnostics, _, _ = calibrate(
            image, manual={"source_points": points, "canonical_points": points}
        )
        self.assertEqual(diagnostics["mode"], "manual")
        self.assertEqual(diagnostics["reference_points"], points)
        np.testing.assert_allclose(H, np.eye(3), atol=1e-8)

    def test_inconsistent_manual_points_rejected(self):
        source = outer_vertices().copy()
        source[0] += [150, 100]
        with self.assertRaises(CalibrationError):
            calibrate(
                np.full((SIZE, SIZE, 3), 180, np.uint8),
                manual={
                    "source_points": source.tolist(),
                    "canonical_points": outer_vertices().tolist(),
                },
            )

    def test_reference_failure_reports_boundary_fallback(self):
        image = np.full((SIZE, SIZE, 3), 220, np.uint8)
        cv2.fillPoly(image, [outer_vertices().astype(np.int32)], (180, 80, 20))
        _, _, diagnostics, _, _ = calibrate(
            image, reference=np.zeros_like(image), reference_H=np.eye(3)
        )
        self.assertEqual(diagnostics["mode"], "automatic_boundary")
        self.assertIn("reference_warning", diagnostics)


class MaskAndRecognitionTests(unittest.TestCase):
    def test_token_and_invalid_pixels_excluded(self):
        tile = make_grid()[9]
        image = np.full((SIZE, SIZE, 3), (30, 170, 70), np.uint8)
        exclusion = np.zeros((SIZE, SIZE), np.uint8)
        valid = np.full_like(exclusion, 255)
        cv2.circle(exclusion, (550, 550), 30, 255, -1)
        valid[:550] = 0
        features, mask = extract_features(
            image, tile, exclusion, np.zeros_like(exclusion), valid
        )
        self.assertGreater(features["valid_pixel_count"], 500)
        self.assertFalse(np.any(mask[exclusion > 0]))
        self.assertFalse(np.any(mask[valid == 0]))

    def test_hue_wraparound(self):
        tile = make_grid()[9]
        hsv = np.full((SIZE, SIZE, 3), (179, 200, 180), np.uint8)
        hsv[:, 550:, 0] = 1
        image = cv2.cvtColor(hsv, cv2.COLOR_HSV2BGR)
        empty = np.zeros((SIZE, SIZE), np.uint8)
        features, _ = extract_features(
            image, tile, empty, empty, np.full_like(empty, 255)
        )
        self.assertLess(
            min(features["circular_mean_h"], 180 - features["circular_mean_h"]), 2
        )
        self.assertGreater(features["hue_concentration"], 0.99)

    def test_insufficient_sample_abstains(self):
        empty = np.zeros((SIZE, SIZE), np.uint8)
        features, _ = extract_features(
            np.zeros((SIZE, SIZE, 3), np.uint8), make_grid()[0], empty, empty, empty
        )
        self.assertIsNone(features)
        self.assertIsNone(TerrainRecognizer().recognize(features)["terrain"])

    def test_blank_token_abstains(self):
        result, _, _ = NumberRecognizer().recognize(
            np.full((128, 128, 3), 220, np.uint8)
        )
        self.assertIsNone(result["number"])

    def test_no_circle_on_uniform_tile(self):
        token, crop, _ = detect_token(
            np.full((SIZE, SIZE, 3), 180, np.uint8), make_grid()[9]
        )
        self.assertEqual(token["status"], "not_detected")
        self.assertIsNone(crop)

    def test_neutral_foreground_occlusion(self):
        image = np.full((SIZE, SIZE, 3), (50, 150, 220), np.uint8)
        reference = image.copy()
        cv2.circle(image, (550, 550), 20, (70, 70, 70), -1)
        result, mask = possible_occlusion(
            image, make_grid()[9], {"status": "not_detected"}, reference
        )
        self.assertTrue(result["possible"])
        self.assertGreater(np.count_nonzero(mask), 1000)

    def test_provisional_profiles_require_review(self):
        result = TerrainRecognizer().recognize(
            {"circular_mean_h": 30, "s": 180, "v": 190}
        )
        self.assertEqual(result["status"], "requires_review")

    def test_rotated_synthetic_glyphs(self):
        # Font-generated fixtures test rotation handling only, not unseen-font accuracy.
        reader = NumberRecognizer()
        font_path = Path("/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf")
        if not font_path.exists():
            self.skipTest("Synthetic fixture font unavailable on this platform.")
        font = ImageFont.truetype(str(font_path), 42)
        for value in [6, 9, 11]:
            for angle in [0, 90, 180]:
                with self.subTest(value=value, angle=angle):
                    image = Image.new("RGB", (128, 128), (240, 195, 125))
                    draw = ImageDraw.Draw(image)
                    color = (180, 35, 35) if value == 6 else (15, 15, 15)
                    box = draw.textbbox((0, 0), str(value), font=font)
                    draw.text(
                        ((128 - (box[2] - box[0])) / 2, 29 - box[1]),
                        str(value),
                        font=font,
                        fill=color,
                    )
                    count = PIPS[value]
                    for i in range(count):
                        x = 64 + (i - (count - 1) / 2) * 7
                        draw.ellipse((x - 2, 77, x + 2, 81), fill=color)
                    patch = cv2.cvtColor(np.asarray(image), cv2.COLOR_RGB2BGR)
                    patch = cv2.warpAffine(
                        patch,
                        cv2.getRotationMatrix2D((63.5, 63.5), angle, 1),
                        (128, 128),
                        borderValue=(125, 195, 240),
                    )
                    result, _, _ = reader.recognize(patch)
                    self.assertEqual(result["number"], value)


class ValidationTests(unittest.TestCase):
    def test_inventory_check_never_rewrites_predictions(self):
        terrains = [k for k, n in TERRAIN_COUNTS.items() for _ in range(n)]
        numbers = [k for k, n in NUMBER_COUNTS.items() for _ in range(n)] + [None]
        tiles = [
            {
                "tile_id": f"t{i}",
                "terrain": t,
                "number": n,
                "token": {"status": "detected" if n else "not_detected"},
                "status": "requires_review",
            }
            for i, (t, n) in enumerate(zip(terrains, numbers, strict=True))
        ]
        before = json.dumps(tiles, sort_keys=True)
        result = validate_board(tiles, {"orientation": "aligned_to_reference"})
        self.assertTrue(result["inventory_consistent"])
        self.assertFalse(result["ready_for_controller"])
        tiles[0]["number"] = 12
        self.assertFalse(validate_board(tiles, {})["inventory_consistent"])
        tiles[0]["number"] = numbers[0]
        self.assertEqual(before, json.dumps(tiles, sort_keys=True))

    def test_unverified_review_is_not_ground_truth(self):
        (ROOT / "outputs/tests").mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "outputs/tests") as directory:
            path = Path(directory) / "review.csv"
            path.write_text(
                "tile_id,verified_terrain,verified_number,verified\nr0c0,forest,8,no\n"
            )
            with self.assertRaises(ValueError):
                read_verified(path)


if __name__ == "__main__":
    unittest.main()
