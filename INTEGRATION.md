# CATAN · two-board integration
Base: jackie/uart-integration a418d23. Ports the full game and I²C/SD driver work from 9628529 and the OpenCV tools from 628ad57. Root = Waveshare ESP32-S3 (8 MB flash / octal PSRAM). wroom_firmware = original ESP32-WROOM (configuration assumes 4 MB flash, onboard DAC). These are different binaries. Work from a fresh extraction. No need to replace sdkconfig or delete your previous project.

## Wiring (GPIO numbers)
Power off before rewiring; both boards need a common ground and 3.3 V logic. S3 UART2 TX43 -> WROOM RX16; WROOM TX17 -> S3 RX44. Select the UART2 header routing on the Waveshare. Use native USB for the S3 console while the link occupies 43/44. PN532 goes on the WROOM in **I²C mode**, SDA32/SCL33; use the switch markings for your exact reader, with pull-ups to 3.3 V. Do not retain its old SPI wiring. FAT-formatted SD module on WROOM: CLK18, MISO19, MOSI23, CS5. GPIO25 DAC goes to the audio amplifier input, not directly to a passive speaker.

## Flash
Open a terminal in this directory, activate ESP-IDF 5.5.5 and list ports with python -m serial.tools.list_ports -v. Identify each port by unplugging one board at a time. From wroom_firmware: idf.py set-target esp32; idf.py build; idf.py -p YOUR_WROOM_PORT -b 115200 flash monitor. Ctrl+] exits monitor. From the root: same commands with esp32s3 and YOUR_S3_PORT. set-target is for first setup only. If esptool stops at Connecting, hold BOOT, tap RESET, release BOOT, then re-list the port before retrying. Do not erase flash as a connection fix.

## First table test
1. Insert an expendable FAT SD card. Expect SD READY on the S3 after real mount/write/read verification. Choose Start Game, load one of three slots, inspect its board, then confirm. First access creates a missing preset on SD.
2. Tap Player 1, then scan one card. Remove it, wait at least 300 ms plus polling time, and try assigning it to Player 2: expect a rejection. Register 3–4 distinct 4- or 7-byte cards. Scans elsewhere should not change the roster.
3. At a private handoff present the wrong player's card, then the right one. Only the right card opens the private view. At a trade check both identities and explicit approvals; cancel once and compare both balances. Confirm a purchase once and compare the bank and player's balances.
4. An accepted ordinary turn roll plays dice; a gameplay 7 plays robber instead. A seven rolled just to determine starting order is not a robber event. Reopening the robber page must not replay the sound. Accepted trades, builds, card draws, accepted scans and rejected game actions have event cues.
5. After a purchase open Save / Storage. Wait for “Saved to SD”, reset both controllers, then Resume Saved Game. Compare roster/UIDs, resources, deck, phase, placements and robber. History text, RNG stream, current UI session and pending approvals are deliberately not resumed. A new game's committed state replaces the previous game; this is one game with two alternating recovery slots.
6. Remove SD between operations, make another move and observe the unsaved state. Reinsert the same card without restarting the S3 and check for a successful retry. A reset before acknowledgment can lose the latest change. Do not treat board storage and game checkpoints as the same files.
7. To import a board, generate board.json following board_recognition/README.md, or start with its published sample. On Mac run python3 -m http.server 8000 --bind 127.0.0.1 in this repo, open http://localhost:8000/board_recognition/review.html in Web Bluetooth-capable Chrome, choose JSON, correct the tiles, select a slot and confirm the BLE transfer. On a compatible phone serve the page via HTTPS. Cancel the Bluetooth chooser to leave storage unchanged. Reload the slot on the board screen. Recognition runs off-board. BLE upload is for a trusted lab environment and has no pairing requirement.

## Tests
c++ -std=c++17 tests/checkpoint.cpp -o /tmp/checkpoint && /tmp/checkpoint
cc -std=c11 tests/storage.c -o /tmp/storage && /tmp/storage

The checkpoint test round-trips game data and card identities. The storage harness tests WROOM protocol logic with simulated media removal, incomplete writes, checksum rejection and fallback. Neither measures electrical or timing requirements. Build workflow is included; no hardware pass or CI run is claimed.
