"""Classical binary glyph matching with pip-guided orientation and abstention."""

from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

VALUES = (2, 3, 4, 5, 6, 8, 9, 10, 11, 12)
PIPS = {2: 1, 3: 2, 4: 3, 5: 4, 6: 5, 8: 5, 9: 4, 10: 3, 11: 2, 12: 1}


def normalize(binary):
    points = cv2.findNonZero(binary)
    if points is None:
        return np.zeros((64, 64), np.uint8)
    x, y, w, h = cv2.boundingRect(points)
    crop = binary[y : y + h, x : x + w]
    scale = min(50 / w, 50 / h)
    w2 = max(1, round(w * scale))
    h2 = max(1, round(h * scale))
    result = np.zeros((64, 64), np.uint8)
    result[
        (64 - h2) // 2 : (64 - h2) // 2 + h2, (64 - w2) // 2 : (64 - w2) // 2 + w2
    ] = cv2.resize(crop, (w2, h2), interpolation=cv2.INTER_NEAREST)
    return result


def extract_glyph(patch):
    patch = cv2.resize(patch, (128, 128), interpolation=cv2.INTER_CUBIC)
    hsv = cv2.cvtColor(patch, cv2.COLOR_BGR2HSV)
    gray = cv2.cvtColor(patch, cv2.COLOR_BGR2GRAY)
    yy, xx = np.indices(gray.shape)
    distance = (xx - 63.5) ** 2 + (yy - 63.5) ** 2
    inside = distance < 44**2
    values = gray[distance < 35**2]
    threshold, _ = cv2.threshold(
        values.reshape(-1, 1), 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU
    )
    threshold = min(float(np.percentile(values, 70)) * 0.85, threshold + 20)
    ink = ((gray < threshold) & inside).astype(np.uint8) * 255
    count, labels, stats, centers = cv2.connectedComponentsWithStats(ink)
    edge_labels = set(np.unique(labels[(distance >= 42**2) & inside]))
    components = [
        k
        for k in range(1, count)
        if stats[k, cv2.CC_STAT_AREA] >= 7 and k not in edge_labels
    ]
    ink = (np.isin(labels, components) * 255).astype(np.uint8)
    ink_pixels = hsv[ink > 0]
    is_red = bool(
        len(ink_pixels)
        and np.median(ink_pixels[:, 2]) > 125
        and np.median(ink_pixels[:, 0]) < 14
    )
    if not components:
        return None, {"status": "unreadable", "reason": "No separated glyph ink."}, ink
    largest = max(stats[k, cv2.CC_STAT_AREA] for k in components)
    digits = [
        k for k in components if stats[k, cv2.CC_STAT_AREA] >= max(55, 0.23 * largest)
    ]
    pips = [
        k
        for k in components
        if 7 <= stats[k, cv2.CC_STAT_AREA] < max(55, 0.23 * largest)
    ]
    glyph = np.isin(labels, digits).astype(np.uint8) * 255
    digit_points = np.argwhere(glyph)
    if not len(digit_points):
        return (
            None,
            {"status": "unreadable", "reason": "No significant glyph component."},
            ink,
        )
    digit_center = digit_points[:, ::-1].mean(axis=0)
    pips = [k for k in pips if np.linalg.norm(centers[k] - digit_center) > 10]
    if not 1 <= len(pips) <= 5:
        return (
            None,
            {
                "status": "requires_review",
                "reason": "Pip row does not establish number orientation.",
                "pip_component_count": len(pips),
                "red_ink": is_red,
            },
            ink,
        )
    pip_centers = np.array([centers[k] for k in pips])
    pip_center = pip_centers.mean(axis=0)
    down = pip_center - digit_center
    angle = np.rad2deg(np.arctan2(down[1], down[0])) - 90
    if len(pips) > 1:
        _, _, vectors = np.linalg.svd(pip_centers - pip_center, full_matrices=False)
        horizontal = vectors[0]
        normal = np.array([-horizontal[1], horizontal[0]])
        if np.dot(normal, down) < 0:
            normal = -normal
        angle = np.rad2deg(np.arctan2(normal[1], normal[0])) - 90
        line_error = float(np.max(np.abs((pip_centers - pip_center) @ normal)))
        if line_error > 5:
            return (
                None,
                {
                    "status": "requires_review",
                    "reason": "Pip components are not collinear.",
                    "pip_component_count": len(pips),
                },
                ink,
            )
    matrix = cv2.getRotationMatrix2D((63.5, 63.5), float(angle), 1)
    upright = cv2.warpAffine(glyph, matrix, (128, 128), flags=cv2.INTER_NEAREST)
    return (
        normalize(upright),
        {
            "status": "segmented",
            "pip_component_count": len(pips),
            "rotation_deg": float(angle),
            "red_ink": is_red,
        },
        ink,
    )


class NumberRecognizer:
    def __init__(self, font_paths=None, template_dir=None):
        self.templates = []
        self.template_source = "rendered_font_prototypes_not_real_labeled_training_data"
        if template_dir:
            for path in sorted(Path(template_dir).glob("*/*.png")):
                value = int(path.parent.name)
                if value not in VALUES:
                    continue
                patch = cv2.imread(str(path))
                if patch is None:
                    raise ValueError(f"Unreadable token template: {path}")
                glyph, _details, _ = extract_glyph(patch)
                if glyph is not None:
                    self.templates.append((value, glyph, path.name))
            if not self.templates:
                raise ValueError("No usable real token templates.")
            missing = set(VALUES) - {value for value, _, _ in self.templates}
            if missing:
                raise ValueError(
                    f"Real templates are missing usable labeled values: {sorted(missing)}"
                )
            self.template_source = "user_labeled_real_token_crops"
        else:
            default = [
                Path("/System/Library/Fonts/Supplemental") / n
                for n in [
                    "Times New Roman.ttf",
                    "Times New Roman Bold.ttf",
                    "Georgia.ttf",
                    "Georgia Bold.ttf",
                    "Arial.ttf",
                ]
            ]
            default += [
                Path("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"),
                Path(
                    "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf"
                ),
            ]
            paths = (
                [Path(p) for p in font_paths]
                if font_paths
                else [p for p in default if p.exists()]
            )
            if not paths:
                raise ValueError(
                    "No serif font found. Supply --font or labeled token templates."
                )
            for path in paths:
                font = ImageFont.truetype(str(path), 64)
                for value in VALUES:
                    canvas = Image.new("L", (180, 110))
                    ImageDraw.Draw(canvas).text(
                        (15, 5), str(value), font=font, fill=255
                    )
                    prototype = (np.asarray(canvas) > 100).astype(np.uint8) * 255
                    self.templates.append((value, normalize(prototype), path.name))

    def recognize(self, patch):
        if patch is None:
            return {"number": None, "status": "not_detected", "score": None}, None, None
        glyph, details, ink = extract_glyph(patch)
        details["template_source"] = self.template_source
        if glyph is None:
            return dict(details, number=None, score=None), glyph, ink
        candidates = [v for v in VALUES if ((v in (6, 8)) == details["red_ink"])]
        scores = {}
        for value, template, name in self.templates:
            if value not in candidates:
                continue
            best = 0.0
            for angle in [-6, -3, 0, 3, 6]:
                rotated = cv2.warpAffine(
                    glyph, cv2.getRotationMatrix2D((31.5, 31.5), angle, 1), (64, 64)
                )
                a = rotated > 127
                b = template > 127
                # Symmetric distance tolerates stroke/font differences without using a neural OCR model.
                da = cv2.distanceTransform((~a).astype(np.uint8), cv2.DIST_L2, 3)
                db = cv2.distanceTransform((~b).astype(np.uint8), cv2.DIST_L2, 3)
                chamfer = (float(da[b].mean()) + float(db[a].mean())) / 2
                dice = (
                    2
                    * np.count_nonzero(a & b)
                    / max(1, np.count_nonzero(a) + np.count_nonzero(b))
                )
                score = 0.5 * dice + 0.5 * np.exp(-chamfer / 3)
                best = max(best, float(score))
            if best > scores.get(value, (-1, ""))[0]:
                scores[value] = (best, name)
        ranking = sorted(scores, key=lambda v: scores[v][0], reverse=True)
        if not ranking:
            return (
                dict(
                    details,
                    number=None,
                    status="requires_review",
                    score=None,
                    reason="No template for candidate values.",
                ),
                glyph,
                ink,
            )
        value = ranking[0]
        score = scores[value][0]
        margin = score - scores[ranking[1]][0] if len(ranking) > 1 else None
        accepted = score >= 0.65 and (margin is None or margin >= 0.06)
        details.update(
            number=value if accepted else None,
            candidate_number=value,
            score=score,
            margin=margin,
            status="recognized" if accepted else "requires_review",
            matching_font_or_template=scores[value][1],
            candidates=[{"number": v, "score": scores[v][0]} for v in ranking],
        )
        return details, glyph, ink
