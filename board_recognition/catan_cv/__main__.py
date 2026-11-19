import argparse
import hashlib
import json
import platform
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageOps

from .board_detection import CalibrationError
from .calibration import calibrate
from .io_utils import read_image, write_json
from .pipeline import prepare_reference, run_image

ROOT = Path(__file__).resolve().parents[1]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Traditional CATAN board recognition and diagnostics."
    )
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ["calibrate", "recognize"]:
        p = sub.add_parser(name)
        p.add_argument("--image", required=True, type=Path)
        p.add_argument("--output", required=True, type=Path)
        p.add_argument("--reference", type=Path)
        p.add_argument("--reference-points", type=Path)
        p.add_argument("--manual-points", type=Path)
        if name == "recognize":
            p.add_argument("--terrain-profiles", type=Path)
            p.add_argument("--font", action="append")
            p.add_argument("--templates", type=Path)
            p.add_argument(
                "--bare-reference",
                action="store_true",
                help="Enable appearance-change occlusion checks against a reference known to have no pieces.",
            )
    p = sub.add_parser("batch")
    p.add_argument("--dataset", type=Path, default=ROOT / "testing_images/our_board")
    p.add_argument("--output", type=Path, default=ROOT / "outputs/demo")
    p = sub.add_parser("select-points")
    p.add_argument("--image", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    for name in ["fit-profiles", "evaluate"]:
        p = sub.add_parser(name)
        p.add_argument("--result", required=True, type=Path)
        p.add_argument("--labels", required=True, type=Path)
        p.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def load_points(path):
    return json.loads(path.read_text()) if path else None


def batch(dataset, output):
    from .report import build_demo

    output.mkdir(parents=True, exist_ok=True)
    supported = {".jpg", ".jpeg", ".png", ".tif", ".tiff"}

    def photos(directory):
        return sorted(p for p in directory.iterdir() if p.suffix.lower() in supported)

    bare = photos(dataset / "board_no_number")
    numbered = photos(dataset / "with_number")
    if not bare:
        raise ValueError("No bare-board reference photos found.")
    reference = prepare_reference(bare[0])
    summary = {
        "reference_image": str(bare[0]),
        "ground_truth_available": False,
        "accuracy": None,
        "runtime": {
            "python": platform.python_version(),
            "opencv": cv2.__version__,
            "numpy": np.__version__,
        },
        "calibration_cases": [],
        "recognition_cases": [],
        "boundary_only_cases": [],
        "manual_cases": [],
        "manual_note": "Manual point selection and synthetic homography tests implemented; no human-picked real-image correspondences supplied.",
    }
    inventory = []
    for image in bare + numbered:
        with Image.open(image) as opened:
            inventory.append(
                {
                    "path": str(image),
                    "raw_dimensions": list(opened.size),
                    "exif_orientation": opened.getexif().get(274, 1),
                    "oriented_dimensions": list(ImageOps.exif_transpose(opened).size),
                    "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                }
            )
        try:
            _, _, diag, _, _ = calibrate(read_image(image))
            summary["boundary_only_cases"].append(
                {
                    "name": str(image.relative_to(dataset)),
                    "status": "success",
                    "fit_rms_px": diag["fit_rms_px"],
                    "orientation": diag["orientation"],
                }
            )
        except (CalibrationError, ValueError) as exc:
            summary["boundary_only_cases"].append(
                {
                    "name": str(image.relative_to(dataset)),
                    "status": "failed",
                    "reason": str(exc),
                }
            )
    for images, key, recognize in [
        (bare, "calibration_cases", False),
        (numbered, "recognition_cases", True),
    ]:
        for image in images:
            directory = ("recognition/" if recognize else "calibration/") + image.stem
            try:
                result = run_image(
                    image,
                    output / directory,
                    recognize=recognize,
                    reference=reference,
                    bare_reference=True,
                )
                diag = result["calibration"]
                case = {
                    "name": image.name,
                    "directory": directory,
                    "status": "success",
                    "mode": diag["mode"],
                    "median_residual_px": diag.get("inlier_median_error_px"),
                    "processing_seconds": result["processing_seconds"],
                }
                if recognize:
                    case.update(
                        validation=result["validation"],
                        recognized_numbers=sum(
                            t["number"] is not None for t in result["tiles"]
                        ),
                    )
                summary[key].append(case)
                print(f"{image.name}: {diag['mode']} -> {output / directory}")
            except (CalibrationError, ValueError, OSError) as exc:
                failure = {
                    "name": image.name,
                    "directory": directory,
                    "status": "failed",
                    "reason": str(exc),
                }
                summary[key].append(failure)
                write_json(output / directory / "failure.json", failure)
                print(f"{image.name}: FAILED: {exc}", file=sys.stderr)
    from .robustness import terrain_consistency

    terrain_consistency(output, summary["calibration_cases"])
    write_json(output / "image_inventory.json", inventory)
    write_json(output / "summary.json", summary)
    build_demo(output, summary)
    print(f"Demo index: {output / 'index.html'}")
    return (
        1
        if any(
            x["status"] == "failed"
            for key in ["calibration_cases", "recognition_cases"]
            for x in summary[key]
        )
        else 0
    )


def main():
    args = parse_args()
    try:
        if args.command == "batch":
            return batch(args.dataset, args.output)
        if args.command == "select-points":
            from .manual_calibration import select_points

            return 0 if select_points(args.image, args.output) else 1
        if args.command in ("fit-profiles", "evaluate"):
            from .evaluation import evaluate, fit_profiles

            (fit_profiles if args.command == "fit-profiles" else evaluate)(
                args.result, args.labels, args.output
            )
            print(f"Saved {args.command}: {args.output}")
            return 0
        reference = (
            prepare_reference(args.reference, load_points(args.reference_points))
            if args.reference
            else None
        )
        result = run_image(
            args.image,
            args.output,
            recognize=args.command == "recognize",
            reference=reference,
            manual=load_points(args.manual_points),
            profiles=getattr(args, "terrain_profiles", None),
            fonts=getattr(args, "font", None),
            templates=getattr(args, "templates", None),
            bare_reference=getattr(args, "bare_reference", False),
        )
        print(
            json.dumps(
                {
                    "output": str(args.output),
                    "calibration_mode": result["calibration"]["mode"],
                    "tile_count": result["tile_count"],
                    "validation": result.get("validation"),
                },
                indent=2,
            )
        )
        return 0
    except (CalibrationError, ValueError, OSError, KeyError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        if hasattr(args, "output") and args.command in (
            "calibrate",
            "recognize",
            "batch",
        ):
            write_json(
                args.output / "failure.json", {"status": "failed", "error": str(exc)}
            )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
