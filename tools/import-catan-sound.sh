#!/bin/sh
# Usage: sh tools/import-catan-sound.sh dice "/path/to/original.wav"
set -eu
case "${1:-}" in dice|build|robber|error|ka_ching|scan) ;; *) echo "Choose dice, build, robber, error, ka_ching, or scan." >&2; exit 2;; esac
[ "$#" = 2 ] && [ -f "$2" ] || { echo "Supply your original audio file." >&2; exit 2; }
command -v ffmpeg >/dev/null || { echo "Install ffmpeg first: brew install ffmpeg" >&2; exit 2; }
dest="$(dirname "$0")/../wroom_firmware/main/sounds/$1.raw"
tmp="$(mktemp "$dest.XXXXXX")"
trap 'rm -f "$tmp"' EXIT
ffmpeg -hide_banner -loglevel error -y -i "$2" -map 0:a:0 -vn -ac 1 -ar 44100 -c:a pcm_u8 -f u8 "$tmp"
[ -s "$tmp" ] && mv "$tmp" "$dest"
