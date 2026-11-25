from .io_utils import write_json


def export_board(path, source, calibration, tiles, validation, elapsed):
    result = {
        "schema_version": "1.0",
        "board_type": "standard_catan",
        "source_image": str(source),
        "coordinate_convention": "EXIF-oriented input; canonical 1100x1100 pixels; x right, y down; rows top to bottom, columns left to right.",
        "calibration": calibration,
        "tile_count": len(tiles),
        "tiles": tiles,
        "validation": validation,
        "processing_seconds": elapsed,
        "evidence": {
            "ground_truth_available": False,
            "accuracy": None,
            "score_interpretation": "Uncalibrated matching scores, not statistical probabilities.",
        },
    }
    write_json(path, result)
    return result
