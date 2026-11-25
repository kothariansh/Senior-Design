import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageOps


def read_image(path):
    with Image.open(path) as image:
        return cv2.cvtColor(
            np.asarray(ImageOps.exif_transpose(image).convert("RGB")), cv2.COLOR_RGB2BGR
        )


def write_json(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(
        json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )


def save_image(path, image):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(path), image):
        raise OSError(f"Could not save image: {path}")
