"""Local, self-contained demo index using generated real-image artifacts."""

import html
import json
from pathlib import Path

from PIL import Image, ImageDraw


def contact_sheet(paths, output, columns=3, cell=(400, 430)):
    paths = list(paths)
    sheet = Image.new(
        "RGB",
        (columns * cell[0], ((len(paths) + columns - 1) // columns) * cell[1]),
        "#eeeeee",
    )
    draw = ImageDraw.Draw(sheet)
    for i, (label, path) in enumerate(paths):
        x = (i % columns) * cell[0]
        y = (i // columns) * cell[1]
        draw.text((x + 10, y + 8), label, fill="black")
        with Image.open(path) as image:
            image.thumbnail((cell[0] - 12, cell[1] - 35))
            sheet.paste(image, (x + (cell[0] - image.width) // 2, y + 28))
    sheet.save(output)


def build_demo(out, summary):
    out = Path(out)
    calibration = summary["calibration_cases"]
    recognition = summary["recognition_cases"]
    panels = [
        (x["name"], out / x["directory"] / "grid_overlay.jpg")
        for x in calibration
        if x["status"] == "success"
    ]
    if panels:
        contact_sheet(panels, out / "calibration_comparison.jpg")
    sections = []
    for case in recognition:
        if case["status"] != "success":
            continue
        directory = case["directory"]
        root = out / directory
        panels = [
            (p.parent.name, p) for p in sorted((root / "tiles").glob("*/original.png"))
        ]
        contact_sheet(panels, root / "tile_review.jpg", columns=5, cell=(220, 245))
        panels = [
            (p.parent.name, p) for p in sorted((root / "tiles").glob("*/token.png"))
        ]
        if panels:
            contact_sheet(panels, root / "token_review.jpg", columns=6, cell=(160, 185))
        panels = []
        for p in sorted((root / "tiles").glob("*/original.png")):
            for name in [
                "original.png",
                "token_mask.png",
                "occlusion_mask.png",
                "terrain_only.png",
            ]:
                panels.append((f"{p.parent.name}: {Path(name).stem}", p.parent / name))
        contact_sheet(panels, root / "mask_review.jpg", columns=4, cell=(190, 215))
        board = json.loads((root / "board.json").read_text())
        review_rows = []
        for tile in board["tiles"]:
            tile_id = tile["tile_id"]
            token_image = (
                f'<img width="128" src="tiles/{tile_id}/token.png">'
                if tile["token"]["status"] == "detected"
                else "No detected token"
            )
            review_rows.append(
                f'<tr><td>{tile_id}</td><td><img width="181" src="tiles/{tile_id}/original.png"></td><td>{token_image}</td><td>{tile["terrain"]}</td><td>{tile["number"] if tile["number"] is not None else "null"}</td><td>{tile["number_recognition"]["status"]}<br>Possible occlusion: {tile["occlusion"]["possible"]}</td></tr>'
            )
        review_page = (
            '<!doctype html><html lang="en"><meta charset="utf-8"><title>Human verification worksheet</title><style>body{font:16px system-ui;margin:32px}td,th{padding:12px;border-bottom:1px solid #ddd;text-align:left}table{border-collapse:collapse}</style><h1>Human verification worksheet</h1><p>All labels below are algorithm predictions. Verify against the original photo and record corrections in a copy of review.csv. No ground-truth labels have been supplied.</p><p><a href="review.csv">Verification CSV</a> | <a href="source_references.jpg">Original photo with geometry</a></p><table><tr><th>Tile ID</th><th>Original tile</th><th>Token crop</th><th>Predicted terrain</th><th>Predicted number</th><th>Diagnostics</th></tr>'
            + "".join(review_rows)
            + "</table></html>"
        )
        (root / "review.html").write_text(review_page, encoding="utf-8")
        v = case["validation"]
        sections.append(f'''<h2>Checkpoint 2: {html.escape(case["name"])}</h2>
<p>Detected token candidates: {v["detected_token_count"]}. Number predictions: {case["recognized_numbers"]}.
Inventory consistency: {v["inventory_consistent"]}. Ready for controller: {v["ready_for_controller"]}.</p>
<div class="grid"><figure><img src="{directory}/annotated_board.jpg"><figcaption>Provisional terrain and number predictions</figcaption></figure>
<figure><img src="{directory}/terrain_only.jpg"><figcaption>Terrain sampling after token and possible occlusion exclusion</figcaption></figure></div>
<p><a href="{directory}/review.html">Human verification worksheet</a> · <a href="{directory}/board.json">Board JSON</a> · <a href="{directory}/review.csv">Blank human verification table</a> ·
<a href="{directory}/hsv_features.csv">HSV feature table</a> · <a href="{directory}/token_review.jpg">Token crops</a> ·
<a href="{directory}/mask_review.jpg">All tile masks</a> · <a href="{directory}/diagnostics.json">Diagnostics</a></p>''')
    rows = []
    for case in calibration:
        residual = case.get("median_residual_px")
        rows.append(
            f'<tr><td>{html.escape(case["name"])}</td><td>{case["status"]}</td><td>{case.get("mode", "-")}</td><td>{residual if residual is not None else "-"}</td><td><a href="{case["directory"]}/calibration.json">Diagnostics</a></td></tr>'
        )
    page = (
        """<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CATAN Traditional CV Development Demo</title><style>
body{font:16px/1.55 system-ui,sans-serif;max-width:1180px;margin:40px auto;padding:0 24px;background:#f6f4ee;color:#202725}
h1,h2{line-height:1.15}a{color:#095f69}.note{padding:18px;background:#fff1cb;border-left:5px solid #b78b25}.grid{display:grid;grid-template-columns:1fr 1fr;gap:20px}
figure{margin:0}img{max-width:100%;height:auto}table{border-collapse:collapse;width:100%;background:white}th,td{text-align:left;padding:10px;border-bottom:1px solid #ddd}
@media(max-width:700px){.grid{grid-template-columns:1fr}}code{background:#e6e4de;padding:2px 4px}</style>
<h1>CATAN Traditional Computer Vision</h1><p>Real-board development demonstration · Python / OpenCV · no neural inference</p>
<div class="note"><strong>Evidence boundary.</strong> All pictures are supplied real photographs. There is no independently labeled Ground Truth.
Feature residuals measure internal registration fit. Inventory agreement is a consistency check, not accuracy.
Terrain profiles are heuristic and provisional. Number templates are rendered font prototypes.
No predictions are rewritten to satisfy inventory constraints.</div>
<h2>Checkpoint 1: different camera angles to one coordinate system</h2>
<p>Eight bare-board photographs use a common automatically calibrated reference. Canonical geometry is 1100 × 1100 pixels with 19 pointy-top tiles in 3–4–5–4–3 rows.
Reference registration preserves orientation for this physical arrangement. Boundary-only mode has a documented 60-degree ambiguity.</p>
<img src="calibration_comparison.jpg" alt="All eight actual board photographs after perspective correction and grid overlay">
<table><tr><th>Image</th><th>Status</th><th>Mode</th><th>Median internal residual (px)</th><th>Details</th></tr>"""
        + "".join(rows)
        + """</table>
<p>Manual calibration is implemented through user-selected point correspondences. No human-selected calibration measurements were supplied for this run.
See <a href="summary.json">summary.json</a> for separate boundary-only tests and failure reasons.</p>"""
        + "".join(sections)
        + """
<p><a href="terrain_consistency.json">Cross-photo terrain consistency</a>: __CONSISTENCY_NOTE__</p><h2>Human review and next evidence</h2><p>Review each image and fill <code>verified_terrain</code>, <code>verified_number</code>, and <code>verified=yes</code> in a copy of review.csv.
Use <code>null</code> for a verified Desert without a number. Keep obscured unknown values unverified.
Profile fitting and evaluation commands are documented in the README. Additional labeled photographs must be held out from tuning.</p>
</html>"""
    )
    consistency = json.loads((out / "terrain_consistency.json").read_text())
    agreement = [
        case["tiles_matching_first_image_predictions"]
        for case in consistency["cases"][1:]
    ]
    note = (
        f"Across {len(agreement)} non-reference bare-board photos, {min(agreement)} to {max(agreement)} of 19 predictions agree with the first image. This measures consistency, not accuracy."
        if agreement
        else "No non-reference comparisons available."
    )
    page = page.replace("__CONSISTENCY_NOTE__", note)
    (out / "index.html").write_text(page, encoding="utf-8")
