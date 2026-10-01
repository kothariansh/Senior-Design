import os
import sys
import json
import base64
from pathlib import Path

try:
    import requests
except ImportError:
    print("[ERROR] Python package 'requests' is not installed.")
    print("Install with:")
    print("  python3 -m pip install requests")
    sys.exit(1)


# ============================================================
# CONFIGURATION
# ============================================================

# MODEL = "gemini-3.8-flash"
MODEL = "gemini-3.5-flash-lite"
# MODEL = "gemini-3.5-flash"
# MODEL = "gemini-3.6-flash"
# MODEL = "gemini-3.7-flash"

API_BASE = "https://generativelanguage.googleapis.com/v1beta/models"

IMAGE_PATH = Path("test/board.jpg")

OUTPUT_PATH = Path("board_setup_gemini.json")


# ============================================================
# CATAN RECOGNITION PROMPT
# ============================================================

PROMPT = """
You are a Catan board setup recognition system.

Analyze the supplied photograph of a standard 3-4 player Catan board
before gameplay begins.

Your task is to identify:

1. All 19 land hex tiles.
2. The terrain type of every land hex.
3. The resource associated with every land hex.
4. The visible number token on every non-desert hex.
5. All visible ports around the board.
6. The trade ratio and resource type of each visible port.

Use this fixed row-major coordinate system:

Row 1: r1c1, r1c2, r1c3
Row 2: r2c1, r2c2, r2c3, r2c4
Row 3: r3c1, r3c2, r3c3, r3c4, r3c5
Row 4: r4c1, r4c2, r4c3, r4c4
Row 5: r5c1, r5c2, r5c3

Allowed terrain values:
forest
hills
pasture
fields
mountains
desert

Allowed resource values:
lumber
brick
wool
grain
ore
none

Number token:
integer from 2 to 12 excluding 7,
or null for desert/unreadable.

For each port return:
- position
- ratio: 2 or 3
- resource: lumber, brick, wool, grain, ore, or null

IMPORTANT:
- Do not invent information that cannot be seen.
- Use null when uncertain.
- Return only JSON matching the requested schema.
"""


# ============================================================
# JSON SCHEMA
# ============================================================

RESPONSE_SCHEMA = {
    "type": "OBJECT",
    "properties": {
        "hexes": {
            "type": "ARRAY",
            "items": {
                "type": "OBJECT",
                "properties": {
                    "position": {
                        "type": "STRING"
                    },
                    "terrain": {
                        "type": ["STRING", "NULL"]
                    },
                    "resource": {
                        "type": ["STRING", "NULL"]
                    },
                    "number": {
                        "type": ["INTEGER", "NULL"]
                    }
                },
                "required": [
                    "position",
                    "terrain",
                    "resource",
                    "number"
                ]
            }
        },
        "ports": {
            "type": "ARRAY",
            "items": {
                "type": "OBJECT",
                "properties": {
                    "position": {
                        "type": "STRING"
                    },
                    "ratio": {
                        "type": ["INTEGER", "NULL"]
                    },
                    "resource": {
                        "type": ["STRING", "NULL"]
                    }
                },
                "required": [
                    "position",
                    "ratio",
                    "resource"
                ]
            }
        }
    },
    "required": [
        "hexes",
        "ports"
    ]
}


# ============================================================
# HELPERS
# ============================================================

def get_api_key():
    key = os.environ.get("GEMINI_API_KEY")

    if not key:
        print("[ERROR] GEMINI_API_KEY is not set.")
        sys.exit(1)

    return key


def load_image(image_path: Path):
    if not image_path.exists():
        print(f"[ERROR] Image not found:")
        print(image_path)
        sys.exit(1)

    suffix = image_path.suffix.lower()

    if suffix in [".jpg", ".jpeg"]:
        mime_type = "image/jpeg"
    elif suffix == ".png":
        mime_type = "image/png"
    elif suffix == ".webp":
        mime_type = "image/webp"
    else:
        print(f"[ERROR] Unsupported image type: {suffix}")
        sys.exit(1)

    with open(image_path, "rb") as f:
        image_b64 = base64.b64encode(f.read()).decode("utf-8")

    return mime_type, image_b64


def validate_board(data):
    errors = []

    if not isinstance(data, dict):
        return ["Top-level response is not a JSON object."]

    hexes = data.get("hexes")
    ports = data.get("ports")

    if not isinstance(hexes, list):
        errors.append("'hexes' is missing or is not a list.")
    else:
        if len(hexes) != 19:
            errors.append(
                f"Expected 19 land hexes, received {len(hexes)}."
            )

    if not isinstance(ports, list):
        errors.append("'ports' is missing or is not a list.")

    return errors


# ============================================================
# GEMINI API CALL
# ============================================================

def analyze_board():

    print("=" * 70)
    print("CATAN BOARD RECOGNITION - GEMINI")
    print("=" * 70)

    print(f"[INFO] Model: {MODEL}")
    print(f"[INFO] Image: {IMAGE_PATH}")

    key = get_api_key()
    mime_type, image_b64 = load_image(IMAGE_PATH)

    url = (
        f"{API_BASE}/{MODEL}:generateContent"
    )

    headers = {
        "x-goog-api-key": key,
        "Content-Type": "application/json"
    }

    payload = {
        "contents": [
            {
                "role": "user",
                "parts": [
                    {
                        "text": PROMPT
                    },
                    {
                        "inline_data": {
                            "mime_type": mime_type,
                            "data": image_b64
                        }
                    }
                ]
            }
        ],
        "generationConfig": {
            "temperature": 0,
            "responseMimeType": "application/json",
            "responseJsonSchema": RESPONSE_SCHEMA
        }
    }

    print("[INFO] Sending image to Gemini...")

    try:
        response = requests.post(
            url,
            headers=headers,
            json=payload,
            timeout=(15, 90) # connection 15secs, response read 90 secs
        )

    except requests.exceptions.RequestException as e:
        print()
        print("[ERROR] Network/API request failed:")
        print(e)
        return

    print(f"[INFO] HTTP status: {response.status_code}")

    if response.status_code != 200:
        print()
        print("[ERROR] Gemini API returned an error:")
        print(response.text[:4000])
        return

    try:
        response_json = response.json()

        text = (
            response_json["candidates"][0]
            ["content"]["parts"][0]["text"]
        )

    except Exception as e:
        print()
        print("[ERROR] Could not parse Gemini response.")
        print(repr(e))
        print()
        print(response.text[:4000])
        return

    try:
        board_data = json.loads(text)

    except json.JSONDecodeError as e:
        print()
        print("[ERROR] Gemini response was not valid JSON.")
        print(e)
        print()
        print("----- RAW MODEL OUTPUT -----")
        print(text)
        print("----------------------------")
        return

    print()
    print("===== DETECTED BOARD =====")
    print(json.dumps(board_data, indent=2))
    print("==========================")

    errors = validate_board(board_data)

    if errors:
        print()
        print("[WARNING] Validation issues:")
        for error in errors:
            print(f"  - {error}")
    else:
        print()
        print("[PASS] Basic JSON validation passed.")
        print("[PASS] 19 land hexes returned.")
        print(
            f"[INFO] Ports returned: "
            f"{len(board_data.get('ports', []))}"
        )

    with open(
        OUTPUT_PATH,
        "w",
        encoding="utf-8"
    ) as f:
        json.dump(
            board_data,
            f,
            indent=2
        )

    print()
    print("[PASS] Saved output:")
    print(OUTPUT_PATH.resolve())

    print()
    print("=" * 70)
    print("ANALYSIS FINISHED")
    print("=" * 70)


if __name__ == "__main__":
    analyze_board()