"""Inventory checks are diagnostics and never modify predictions."""

from collections import Counter

from .number_recognition import VALUES

TERRAIN_COUNTS = {
    "forest": 4,
    "pasture": 4,
    "fields": 4,
    "hills": 3,
    "mountains": 3,
    "desert": 1,
}
NUMBER_COUNTS = {2: 1, 3: 2, 4: 2, 5: 2, 6: 2, 8: 2, 9: 2, 10: 2, 11: 2, 12: 1}


def validate_board(tiles, calibration):
    terrain = Counter(t["terrain"] for t in tiles if t["terrain"] is not None)
    numbers = Counter(t["number"] for t in tiles if t["number"] is not None)
    tokens = sum(t["token"]["status"] == "detected" for t in tiles)
    issues = []
    if len(tiles) != 19:
        issues.append("Expected exactly 19 tiles.")
    if len({t["tile_id"] for t in tiles}) != len(tiles):
        issues.append("Duplicate tile IDs.")
    if dict(terrain) != TERRAIN_COUNTS:
        issues.append("Predicted terrain inventory does not match the standard board.")
    if dict(numbers) != NUMBER_COUNTS:
        issues.append("Predicted number inventory does not match the standard board.")
    if tokens != 18:
        issues.append(f"Expected 18 detected tokens; found {tokens}.")
    for t in tiles:
        if t["terrain"] == "desert" and t["number"] is not None:
            issues.append(f"{t['tile_id']}: number on predicted Desert.")
        if t["terrain"] != "desert" and t["number"] is None:
            issues.append(f"{t['tile_id']}: missing or unreadable number.")
        if t["number"] is not None and t["number"] not in VALUES:
            issues.append(f"{t['tile_id']}: invalid number.")
    review = [t["tile_id"] for t in tiles if t["status"] != "recognized"]
    orientation_resolved = (
        calibration.get("orientation") != "ambiguous_modulo_60_degrees"
    )
    return {
        "valid": not issues,
        "inventory_consistent": not issues,
        "ready_for_controller": not issues and not review and orientation_resolved,
        "issues": issues,
        "requires_review": review,
        "orientation_resolved": orientation_resolved,
        "terrain_counts": dict(terrain),
        "number_counts": {str(k): v for k, v in sorted(numbers.items())},
        "detected_token_count": tokens,
        "message": "Inventory consistency is not recognition accuracy. Predictions have not been forced to match counts.",
    }
