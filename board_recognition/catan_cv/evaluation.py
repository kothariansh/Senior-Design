"""Evaluation requires explicit human verification; generated predictions are not labels."""

import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

from .io_utils import write_json
from .number_recognition import VALUES
from .terrain_recognition import RESOURCES


def read_verified(path):
    rows = {}
    with Path(path).open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("verified", "").strip().lower() != "yes":
                continue
            terrain = row["verified_terrain"].strip().lower()
            number = row["verified_number"].strip().lower()
            if terrain not in RESOURCES:
                raise ValueError(f"Invalid verified terrain for {row['tile_id']}")
            if number == "null":
                number = None
            elif number.isdigit() and int(number) in VALUES:
                number = int(number)
            else:
                raise ValueError(
                    "Verified numbers must be a supported number or explicit null."
                )
            if row["tile_id"] in rows:
                raise ValueError("Duplicate verified tile ID.")
            rows[row["tile_id"]] = {"terrain": terrain, "number": number}
    if not rows:
        raise ValueError(
            "No manually verified rows. Predictions cannot be used as Ground Truth."
        )
    return rows


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fit_profiles(result_path, labels_path, output):
    result = json.loads(Path(result_path).read_text())
    labels = read_verified(labels_path)
    groups = defaultdict(list)
    for tile in result["tiles"]:
        if tile["tile_id"] not in labels:
            continue
        f = tile["hsv"]
        if f is None:
            continue
        groups[labels[tile["tile_id"]]["terrain"]].append(
            [f["circular_mean_h"], f["s"], f["v"]]
        )
    if set(groups) != set(RESOURCES):
        raise ValueError(
            "At least one verified real-image sample is required for each of the six terrain classes."
        )
    write_json(
        output,
        {
            "version": 1,
            "provenance": "Human-verified real-image training ROIs; not held-out validation.",
            "verified_labels": True,
            "distance_scales": [7.0, 45.0, 50.0],
            "profiles": dict(groups),
            "training_source_image": result["source_image"],
            "training_result_sha256": file_hash(result_path),
            "labels_sha256": file_hash(labels_path),
        },
    )


def evaluate(result_path, labels_path, output):
    result = json.loads(Path(result_path).read_text())
    labels = read_verified(labels_path)
    ids = {t["tile_id"] for t in result["tiles"]}
    if not set(labels) <= ids:
        raise ValueError("Labels contain tile IDs absent from the result.")
    cases = []
    for tile in result["tiles"]:
        if tile["tile_id"] not in labels:
            continue
        truth = labels[tile["tile_id"]]
        cases.append(
            {
                "tile_id": tile["tile_id"],
                "terrain_correct": tile["terrain"] == truth["terrain"],
                "number_correct": tile["number"] == truth["number"],
            }
        )
    n = len(cases)
    write_json(
        output,
        {
            "evaluated_tiles": n,
            "terrain_accuracy": sum(c["terrain_correct"] for c in cases) / n,
            "number_accuracy_including_expected_null": sum(
                c["number_correct"] for c in cases
            )
            / n,
            "exact_board_match": all(
                c["terrain_correct"] and c["number_correct"] for c in cases
            )
            if n == 19
            else None,
            "evaluation_scope": "Human-verified development-image comparison. This alone is not held-out generalization accuracy.",
            "result_sha256": file_hash(result_path),
            "labels_sha256": file_hash(labels_path),
            "cases": cases,
        },
    )
