import csv
import time
from pathlib import Path

import cv2
import numpy as np

from .calibration import calibrate
from .export_json import export_board
from .hex_grid import make_grid, outer_vertices, overlay, tile_mask
from .io_utils import read_image, save_image, write_json
from .number_recognition import NumberRecognizer
from .terrain_recognition import TerrainRecognizer, extract_features
from .token_detection import detect_token, possible_occlusion
from .validation import validate_board


def prepare_reference(path, manual=None):
    image = read_image(path)
    warped, H, diagnostics, _, _ = calibrate(image, manual=manual)
    return {
        "image": image,
        "H": H,
        "warped": warped,
        "diagnostics": diagnostics,
        "path": str(path),
    }


def save_calibration(out, image, warped, H, diagnostics, valid, mask):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    save_image(out / "calibrated.jpg", warped)
    save_image(out / "grid_overlay.jpg", overlay(warped))
    save_image(out / "valid_pixels.png", valid)
    marked = image.copy()
    vertices = cv2.perspectiveTransform(outer_vertices()[None], np.linalg.inv(H))[0]
    cv2.polylines(marked, [np.rint(vertices).astype(np.int32)], True, (0, 255, 255), 5)
    for i, point in enumerate(diagnostics.get("reference_points") or []):
        x, y = np.rint(point).astype(int)
        cv2.circle(marked, (x, y), 12, (0, 0, 255), -1)
        cv2.putText(
            marked,
            str(i),
            (x + 14, y - 10),
            cv2.FONT_HERSHEY_SIMPLEX,
            1,
            (0, 0, 255),
            3,
        )
    for tile in make_grid():
        polygon = cv2.perspectiveTransform(
            np.float32(tile["polygon"])[None], np.linalg.inv(H)
        )[0]
        cv2.polylines(
            marked, [np.rint(polygon).astype(np.int32)], True, (220, 40, 230), 2
        )
    save_image(out / "source_references.jpg", marked)
    if mask is not None:
        save_image(out / "sea_mask.png", mask)
    write_json(out / "calibration.json", diagnostics)
    write_json(out / "grid.json", make_grid())


def run_image(
    source,
    out,
    recognize=False,
    reference=None,
    manual=None,
    profiles=None,
    fonts=None,
    templates=None,
    bare_reference=False,
):
    started = time.perf_counter()
    out = Path(out)
    image = read_image(source)
    warped, H, calibration, valid, mask = calibrate(
        image,
        manual=manual,
        reference=reference["image"] if reference else None,
        reference_H=reference["H"] if reference else None,
    )
    if reference:
        calibration["reference_image"] = reference["path"]
        calibration["reference_anchor_orientation"] = reference["diagnostics"][
            "orientation"
        ]
    save_calibration(out, image, warped, H, calibration, valid, mask)
    if not recognize:
        result = {
            "source_image": str(source),
            "calibration": calibration,
            "tile_count": 19,
            "processing_seconds": time.perf_counter() - started,
        }
        write_json(out / "result.json", result)
        return result
    terrain_reader = TerrainRecognizer(profiles)
    number_reader = NumberRecognizer(fonts, templates)
    annotated = overlay(warped)
    tokens_image = overlay(warped)
    all_sampling = np.zeros_like(valid)
    all_exclusion = np.zeros_like(valid)
    records = []
    review = []
    features_table = []
    for tile in make_grid():
        token, crop, token_mask = detect_token(warped, tile)
        occlusion, occlusion_mask = possible_occlusion(
            warped,
            tile,
            token,
            reference["warped"]
            if reference
            and bare_reference
            and calibration["mode"] == "automatic_reference"
            else None,
        )
        features, sampling = extract_features(
            warped, tile, token_mask, occlusion_mask, valid
        )
        terrain = terrain_reader.recognize(features)
        number, glyph, ink = number_reader.recognize(crop)
        if occlusion["possible"]:
            number.update(
                number=None,
                status="occluded",
                reason="Possible foreground occlusion; no hidden number inferred.",
            )
        if terrain["terrain"] == "desert" and token["status"] == "not_detected":
            number.update(
                number=None,
                status="expected_absent",
                reason="No number expected on predicted Desert; terrain remains subject to review.",
            )
        status = (
            "recognized"
            if terrain["status"] == "recognized"
            and number["status"] in ("recognized", "expected_absent")
            and not occlusion["possible"]
            else "requires_review"
        )
        record = dict(
            tile,
            terrain=terrain["terrain"],
            resource=terrain["resource"],
            number=number["number"],
            hsv=features,
            terrain_score=terrain["score"],
            number_score=number["score"],
            status=status,
            terrain_recognition=terrain,
            number_recognition=number,
            token=token,
            occlusion=occlusion,
        )
        records.append(record)
        cx, cy = np.rint(tile["center"]).astype(int)
        tile_out = out / "tiles" / tile["tile_id"]
        tile_out.mkdir(parents=True, exist_ok=True)

        def tile_crop(im, cx=cx, cy=cy):
            return im[cy - 90 : cy + 91, cx - 90 : cx + 91]

        save_image(tile_out / "original.png", tile_crop(warped))
        save_image(tile_out / "interior_mask.png", tile_crop(tile_mask(tile)))
        save_image(tile_out / "token_mask.png", tile_crop(token_mask))
        save_image(tile_out / "occlusion_mask.png", tile_crop(occlusion_mask))
        save_image(tile_out / "terrain_mask.png", tile_crop(sampling))
        sampled = cv2.bitwise_and(warped, warped, mask=sampling)
        save_image(tile_out / "terrain_only.png", tile_crop(sampled))
        for filename, artifact in [
            ("token.png", crop),
            ("glyph.png", glyph),
            ("ink.png", ink),
        ]:
            if artifact is not None:
                save_image(tile_out / filename, artifact)
            else:
                (tile_out / filename).unlink(missing_ok=True)
        all_sampling = cv2.bitwise_or(all_sampling, sampling)
        all_exclusion = cv2.bitwise_or(
            all_exclusion, cv2.bitwise_or(token_mask, occlusion_mask)
        )
        if token["center"]:
            point = tuple(np.rint(token["center"]).astype(int))
            r = round(token["radius"])
            for display in (annotated, tokens_image):
                cv2.circle(display, point, r, (0, 0, 255), 2)
        if occlusion["possible"]:
            cv2.circle(annotated, (cx, cy), 45, (0, 150, 255), 3)
        label = f"{terrain['terrain'] or '?'} / {number['number'] if number['number'] is not None else '-'}"
        cv2.putText(
            annotated,
            label,
            (cx - 70, cy + 50),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.43,
            (0, 0, 0),
            3,
            cv2.LINE_AA,
        )
        cv2.putText(
            annotated,
            label,
            (cx - 70, cy + 50),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.43,
            (255, 255, 255),
            1,
            cv2.LINE_AA,
        )
        review.append(
            {
                "tile_id": tile["tile_id"],
                "predicted_terrain": terrain["terrain"],
                "predicted_number": number["number"],
                "verified_terrain": "",
                "verified_number": "",
                "verified": "no",
                "notes": "",
            }
        )
        features_table.append(
            dict(
                tile_id=tile["tile_id"],
                terrain=terrain["terrain"],
                number=number["number"],
                terrain_score=terrain["score"],
                number_score=number["score"],
                **{
                    k: features[k] if features else None
                    for k in [
                        "h",
                        "s",
                        "v",
                        "circular_mean_h",
                        "mean_s",
                        "mean_v",
                        "valid_pixel_count",
                    ]
                },
            )
        )
    save_image(out / "annotated_board.jpg", annotated)
    save_image(out / "token_detections.jpg", tokens_image)
    save_image(out / "terrain_sampling_mask.png", all_sampling)
    save_image(out / "exclusion_mask.png", all_exclusion)
    save_image(
        out / "terrain_only.jpg", cv2.bitwise_and(warped, warped, mask=all_sampling)
    )
    for filename, rows in [
        ("review.csv", review),
        ("hsv_features.csv", features_table),
    ]:
        if filename == "review.csv" and (out / filename).exists():
            continue  # Never overwrite a user's verification work on reruns.
        with (out / filename).open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    validation = validate_board(records, calibration)
    result = export_board(
        out / "board.json",
        source,
        calibration,
        records,
        validation,
        time.perf_counter() - started,
    )
    write_json(
        out / "diagnostics.json",
        {
            "validation": validation,
            "terrain_profile_provenance": terrain_reader.config["provenance"],
            "number_template_source": number_reader.template_source,
            "ground_truth": "Not supplied; review.csv contains blank verification fields.",
        },
    )
    return result
