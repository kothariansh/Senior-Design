# CATAN Traditional Computer Vision

Python/OpenCV board calibration and recognition for ECE 49022. This directory is independent of the ESP32 firmware. It does not modify or communicate with NFC, UART, LVGL, audio, microSD, or hardware-design code.

## Current result

Start with the [published demo results](demo_results/README.md) to inspect calibration overlays, terrain masks, token crops, and the example JSON directly on GitHub. These are selected artifacts from the supplied development photographs.

To reproduce the complete local report, follow the setup below and run `python -m catan_cv batch` using the configured environment. Then open `outputs/demo/index.html`. Generated `outputs/` are not committed.

| Development check | Observed result |
| --- | --- |
| Reference-based calibration | All 8 bare-board photos and `number1.jpeg` accepted |
| Non-reference-photo SIFT median inlier residual | 0.47–0.90 canonical pixels; internal fit residual, not alignment accuracy |
| Boundary-only calibration | 4/8 bare-board photos accepted; numbered photo accepted |
| Boundary-only rejections | `3.jpeg`, `4.jpeg`, `7.jpeg`: clipped corners; `6.jpeg`: irregular six-corner fit |
| Manual calibration | Implemented; synthetic geometry tests pass; no human-picked real-photo correspondences supplied |
| Numbered-photo output | 19 terrain predictions; 18 circle detections and 18 accepted number predictions |
| Possible occlusion | `r2c3` flagged; conservative center mask excludes the possible piece and shadow |
| Desert handling | Predicted Desert has `number: null`; no hidden number is inferred |
| Inventory diagnostics | Terrain and number counts match the standard board without forced reassignment |
| Cross-photo terrain consistency | Other bare photos match 16–17 of the first photo's 19 predictions |
| Independent Ground Truth / accuracy | Not available / not reported |
| Automated tests | 21 unit tests, including rotated synthetic 6/9/11 glyphs |

Both checkpoints are suitable for an **explainable development demonstration**. They are not a validated production recognizer. Terrain predictions remain `requires_review`, and `ready_for_controller` is false. The cross-photo agreement metric measures consistency, not accuracy.

The legacy `MLLM_Gemini.py` prototype is retained separately; see [its original documentation](LEGACY_GEMINI_README.md). It is not used by the traditional pipeline. The published dataset contains only the nine project-board photographs, not the unrelated online-image examples.

## Setup and reproducible commands

Run commands from this directory:

```bash
cd board_recognition  # From the repository root
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m catan_cv batch
.venv/bin/python -m unittest discover -s tests -v
```

Each teammate should create their own `.venv`; it is not included in Git. `requirements-lock.txt` records the runtime versions used here: Python 3.15.0, OpenCV 5.0.0, NumPy 2.5.4, and Pillow 13.0.0. Python is recorded in the generated runtime metadata, rather than installed by the requirements file. The implementation targets Python 3.11 or later; versions other than the recorded environment have not been tested.

For identical package versions on a compatible Python/platform:

```bash
.venv/bin/python -m pip install -r requirements-lock.txt
```

Number templates use locally installed fonts. The current macOS run uses Times New Roman, Times New Roman Bold, Georgia, Georgia Bold, and Arial. The recognizer also searches for DejaVu/Liberation serif fonts on Linux. Different fonts can change recognition scores. Use repeated `--font /path/to/font.ttf` arguments to choose explicit fonts. No font files or pretrained OCR models are downloaded by the pipeline.

Run the first checkpoint on a specific photo:

```bash
.venv/bin/python -m catan_cv calibrate \
  --image testing_images/our_board/board_no_number/6.jpeg \
  --reference testing_images/our_board/board_no_number/1.jpeg \
  --output outputs/single_calibration
```

Run the second checkpoint:

```bash
.venv/bin/python -m catan_cv recognize \
  --image testing_images/our_board/with_number/number1.jpeg \
  --reference testing_images/our_board/board_no_number/1.jpeg \
  --bare-reference \
  --output outputs/single_recognition
```

Omit `--reference` to test boundary-only automatic calibration. An unreliable or clipped frame is rejected with a nonzero exit code and `failure.json`. When reference matching fails, the pipeline attempts boundary calibration and records the fallback and orientation ambiguity.

`--bare-reference` enables appearance-change checks and should only be used when the reference has no pieces and the terrain arrangement is unchanged. It is not necessary for circle or number recognition. Avoid it when tiles have been rearranged.

The pipeline runs locally. It uses no neural networks, vision-language models, cloud vision APIs, or network requests. Network access is needed only when installing dependencies.

## Geometry and orientation

All images are loaded after EXIF orientation correction. Coordinates in manual files refer to that oriented image.

- Canonical output: 1100 × 1100 pixels, origin at the top left; x increases right, y increases down.
- Canonical board center: `(550, 550)`.
- Sea-frame circumradius: 500 pixels.
- Pointy-top tile circumradius: 90 pixels; rows are `3–4–5–4–3`.
- Tile IDs: `r0c0` through `r4c2`, with columns counted left to right within each row.
- Outer corner order: top-left, top-right, right, bottom-right, bottom-left, left.
- Tile radius / frame radius = 0.18 is a geometry prior for this supplied board edition, not a measurement of arbitrary CATAN editions.

Blue HSV segmentation, morphological closing, convex hulls, and polygon approximation locate the sea frame. Six corner correspondences are fitted with `findHomography`. Four supplied correspondences use `getPerspectiveTransform`. Fits are rejected for degenerate points, excessive residual, invisible tile centers, or a projective horizon through the board.

A regular sea frame has a 60-degree orientation ambiguity. Boundary-only output explicitly reports `ambiguous_modulo_60_degrees`. The batch uses the first bare-board image as an anchor. SIFT features, a ratio test, RANSAC, inlier coverage, and residual checks align subsequent photos to that anchor. Its absolute physical orientation remains a documented convention, not an inferred global north.

Reference registration estimates geometry from image features. It does **not** copy terrain labels, number values, or a fixed arrangement into recognition output. It works best with the same printed board arrangement. Generalization to rearranged tiles, different editions, or heavy occlusion remains unvalidated.

## Manual calibration

Capture six visible physical sea-frame corners:

```bash
.venv/bin/python -m catan_cv select-points \
  --image testing_images/our_board/board_no_number/2.jpeg \
  --output config/manual_2.json

.venv/bin/python -m catan_cv calibrate \
  --image testing_images/our_board/board_no_number/2.jpeg \
  --manual-points config/manual_2.json \
  --output outputs/manual_2
```

Click clockwise, starting at the physical corner that should map to canonical top-left. Use the same physical starting corner across images. Press `U` to undo, Enter to save, or Escape to cancel. This command requires a desktop OpenCV GUI session; it was not exercised through a human click session during development.

A manual JSON file contains `source_points` and corresponding `canonical_points`, each a list of `[x, y]` pairs. Four or more accurate correspondences are accepted. Do not guess invisible frame corners. For a clipped image, use visible internal landmarks with known canonical positions or a suitable reference photograph. `--reference-points` can manually calibrate the reference anchor itself.

## Recognition algorithms

1. Generate each tile's interior polygon from the shared grid.
2. Search near its center with Hough circles, plausible radius limits, and cream-color support.
3. Refine circle location and radius using radial image gradients. The nominal token radius is about 23 canonical pixels; center offsets up to 48 pixels are allowed.
4. Normalize token crops to 128 × 128 pixels. Threshold ink using Otsu with a documented brightness adjustment; separate connected glyph and pip components.
5. Use the pip row to establish orientation, including the 6/9 direction. Pip components can merge, so their count is diagnostic and does not force the predicted value.
6. Compare normalized glyphs against rendered font prototypes using Dice overlap and symmetric chamfer distance. Scores below 0.65 or runner-up margins below 0.06 abstain. Red ink limits candidates to 6/8 for this board edition. Two-digit values are matched as complete glyphs.
7. Flag possible central neutral/dark foreground when no reliable circle is found. A bare-board reference optionally strengthens this check. This is an occlusion heuristic, not a reliable semantic Robber detector; a partially readable covered token can escape this check.
8. Subtract token masks and conservative occlusion masks from eroded tile interiors. Remove invalid warp pixels and extreme intensities.
9. Extract median HSV, saturation-weighted circular mean hue, hue concentration, brightness statistics, and an HS histogram.
10. Compare representative HSV with reference prototypes using circular hue distance. Export paired records by Tile ID and diagnose inventories without changing predictions.

`config/terrain_profiles.json` contains **heuristic prototypes adjusted against unlabeled development feature ranges**. These are not manually verified real-image reference profiles. All terrain predictions remain provisional until suitable labels are supplied. Color histograms are exported for diagnostics and future classical classifiers; they are not currently used in matching. White balance, shadows, glare, and tile artwork can change the result, as the cross-photo consistency report demonstrates.

Number template scores are uncalibrated similarity scores. Rendered-font fixtures are only a baseline; no accuracy on independently labeled token crops is claimed. HOG + SVM is deferred until enough real labeled samples exist for meaningful training and held-out evaluation.

## Outputs and human review

`outputs/demo/index.html` links the two checkpoints and all important artifacts. `outputs/` and `.venv/` are locally generated and excluded by this directory's `.gitignore`.

| Artifact | Purpose |
| --- | --- |
| `summary.json` | Per-photo successes/failures; separate boundary/reference/manual evidence |
| `image_inventory.json` | Original/oriented dimensions, EXIF orientation, source SHA-256 hashes |
| `calibration_comparison.jpg` | All eight actual calibration overlays |
| `terrain_consistency.json` | Cross-photo prediction agreement, not accuracy |
| `calibration/<image>/source_references.jpg` | Original photo with candidate corners and projected tile grid |
| `calibration/<image>/calibrated.jpg` | Corrected board |
| `calibration/<image>/grid_overlay.jpg` | Shared 19-tile geometry |
| `recognition/number1/annotated_board.jpg` | Terrain/number predictions and possible occlusion |
| `recognition/number1/token_detections.jpg` | Detected token circles |
| `recognition/number1/terrain_only.jpg` | Actual pixels used for terrain features |
| `recognition/number1/tiles/<id>/` | Original ROI, all exclusion/sampling masks, token, ink, normalized glyph |
| `recognition/number1/mask_review.jpg` | Per-tile original, token mask, occlusion mask, terrain-only montage |
| `recognition/number1/token_review.jpg` | All detected real token crops |
| `recognition/number1/review.html` | Visual worksheet with original tile, token crop, predictions, and status |
| `recognition/number1/review.csv` | Blank human-verification fields alongside predictions |
| `recognition/number1/hsv_features.csv` | Representative HSV and scores for all 19 tiles |
| `recognition/number1/board.json` | Stable schema, geometry, paired results, uncertainty, diagnostics |

Before rerunning into an output directory, copy any artifacts you need to preserve. Generated images and JSON are replaced, but an existing `review.csv` is **never overwritten**. Use separate output directories for separate experiments or sources.

To establish actual Ground Truth, inspect the original photograph and the per-tile crops. In a copy of `review.csv`, fill `verified_terrain`, `verified_number`, and set `verified=yes` only for independently checked rows. Use lowercase terrain names and explicit `null` for a verified Desert with no number. Leave unreadable or hidden numbers unverified; never fill them using inventory counts.

```bash
.venv/bin/python -m catan_cv evaluate \
  --result outputs/demo/recognition/number1/board.json \
  --labels config/verified_number1.csv \
  --output outputs/evaluation_number1.json
```

The project owner subsequently reported that the predictions were correct after visual review in a local Numbers worksheet. That worksheet has not been imported into the evaluation format, and the archived automatic outputs retain their original review flags. This is a development-image review, not an independent held-out accuracy measurement. No `verified_number1.csv` is supplied or fabricated. Evaluation refuses tables without verified rows. The evaluation command reports metrics only for the explicitly verified subset and identifies the result as a development-image comparison.

Fit real terrain reference profiles after all six terrain classes have at least one verified example:

```bash
.venv/bin/python -m catan_cv fit-profiles \
  --result outputs/demo/recognition/number1/board.json \
  --labels config/verified_number1.csv \
  --output config/terrain_profiles_verified.json
```

Pass `--terrain-profiles config/terrain_profiles_verified.json` on a subsequent recognition run. Evaluate on separately captured, independently labeled photographs; the training image is not a held-out test.

To supply real number templates, place **human-labeled** token crops under a directory such as `config/token_templates/8/sample_01.png`, with one directory for each value. Pass `--templates config/token_templates`. Use the crop format produced by this pipeline, including the token and pips. Never automatically promote model predictions to labeled training examples.

## JSON contract

`config/board.schema.json` describes the schema. The top-level `schema_version` is `1.0`; each of 19 tile records contains geometry, terrain/resource/number, HSV features, scores, detailed recognition results, and an overall status.

- `number: null` must be interpreted together with `number_recognition.status`: expected absence, unreadable token, missing circle, or occlusion are different cases.
- `occlusion.possible` is an explicit warning, not confirmed Robber identity.
- `validation.valid` and `inventory_consistent` indicate standard inventory consistency only.
- `ready_for_controller` additionally requires resolved orientation and no outstanding tile review statuses. This flag is not permission or implementation for firmware integration.
- Recognition `score` fields are not probabilities.
- `processing_seconds` is observed local runtime including output writes; it excludes initial reference preparation in batch mode.

## Tests and development structure

```bash
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python -m pip install -r requirements-dev.txt
.venv/bin/ruff check --no-cache catan_cv tests
.venv/bin/ruff format --no-cache --check catan_cv tests
```

| Module | Responsibility |
| --- | --- |
| `board_detection.py` | Sea-frame segmentation and six-corner candidate extraction |
| `calibration.py`, `manual_calibration.py` | Homography, reference registration, manual correspondences |
| `hex_grid.py` | One shared coordinate convention and 19-tile ROIs |
| `token_detection.py` | Circle detection, refinement, masks, possible occlusion |
| `number_recognition.py` | Ink segmentation, pip orientation, classical template matching |
| `terrain_recognition.py` | Masked HSV features and prototype matching |
| `validation.py`, `export_json.py` | Inventory diagnostics and structured output |
| `pipeline.py`, `__main__.py` | End-to-end commands and batch execution |
| `report.py`, `robustness.py`, `evaluation.py` | Demo assets, consistency diagnostics, human-label evaluation |

Synthetic tests validate implementation behavior only. Final development observations above come from the supplied real photographs. Outstanding work requires human labels and additional real photographs with changed arrangements, lighting, rotations, camera distances, and Robber positions. No full-board or per-class accuracy is claimed before that evidence exists.
