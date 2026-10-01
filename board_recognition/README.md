# CATAN Board Recognition

Optional board-setup recognition module for the CATAN Digital Trading Game Edition.

## Pipeline

Board image -> Gemini multimodal recognition -> structured JSON -> user verification -> ESP32/game-state initialization

## Current Model

Google Gemini 3.5 Flash Lite through the Gemini Developer API Free Tier.

The API key is intentionally NOT stored in this repository. Each developer should use their own Gemini API key.

## Setup

Python 3 is required.

Install the dependency:

    python3 -m pip install requests

Set your own Gemini API key:

    export GEMINI_API_KEY='YOUR_API_KEY'

## Run

From the `board_recognition` directory:

    python3 MLLM_Gemini.py

The current prototype reads the configured test image and produces:

    board_setup_gemini.json

The JSON contains the detected CATAN hex positions, terrain/resource types, number tokens, and visible ports.

## Current Status

Proof of concept is working.

On the initial clean test board, the model returned all 19 land hexes and all 18 non-desert number tokens correctly. Additional testing with real photographs and different board configurations is planned.

## Security

Never commit API keys to this repository.
