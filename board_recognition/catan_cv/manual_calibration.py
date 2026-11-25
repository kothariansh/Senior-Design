"""Interactive six-frame-corner capture, saved as explicit correspondences."""

import cv2

from .hex_grid import outer_vertices
from .io_utils import read_image, write_json


def select_points(source, output):
    image = read_image(source)
    scale = min(1.0, 1200 / max(image.shape[:2]))
    preview = cv2.resize(image, None, fx=scale, fy=scale)
    points = []
    window = "Manual calibration: click 6 frame corners clockwise; Enter saves, U undoes, Esc cancels"

    def on_mouse(event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN and len(points) < 6:
            points.append([x / scale, y / scale])

    cv2.namedWindow(window)
    cv2.setMouseCallback(window, on_mouse)
    print(
        "Click six physical sea-frame corners clockwise. First selected corner maps to canonical top-left."
    )
    print(
        "Choose the same physical first corner across photographs. Never click a clipped or guessed corner."
    )
    while True:
        display = preview.copy()
        for i, (x, y) in enumerate(points):
            p = (round(x * scale), round(y * scale))
            cv2.circle(display, p, 6, (0, 0, 255), -1)
            cv2.putText(
                display, str(i), p, cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2
            )
        cv2.imshow(window, display)
        key = cv2.waitKey(30) & 255
        if key == 27:
            cv2.destroyAllWindows()
            return False
        if key in (ord("u"), 8) and points:
            points.pop()
        if key in (13, 10) and len(points) == 6:
            break
    cv2.destroyAllWindows()
    write_json(
        output,
        {
            "source_image": str(source),
            "coordinate_space": "EXIF-oriented source pixels",
            "source_points": points,
            "canonical_points": outer_vertices().tolist(),
            "provenance": "User clicked reference points.",
        },
    )
    print(f"Saved manual correspondences: {output}")
    return True
