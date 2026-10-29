# CATAN WROOM peripheral controller

Build this directory for **ESP32**. The repository root is the separate ESP32-S3
touchscreen application.

## PN532 wiring

Power off before changing the PN532 interface switches/jumpers. Select **I2C**
using the markings for your module, then power-cycle it.

| PN532 | ESP32-WROOM-32 |
| --- | --- |
| SDA | GPIO32 |
| SCL | GPIO33 |
| VCC | 3.3V |
| GND | GND |

The reader uses 7-bit address **0x24** at **100 kHz**. No IRQ connection is
required. Disconnect its former SPI connections (GPIO18/19/23/21). Check that
SDA and SCL each have a pull-up to **3.3V**; use the module's existing pull-ups
or suitable external resistors (typically 4.7 kΩ). Internal pull-ups are enabled
as a supplement.

The SD card retains SPI2: SCK GPIO18, MISO GPIO19, MOSI GPIO23, CS GPIO5.
SD initialization and autosave remain commented out. UART2 retains TX GPIO17,
RX GPIO16 and 115200 baud. Audio retains DAC GPIO25.

## Build, flash, and monitor

The PN532 component uses `driver/i2c_master.h` and `esp_driver_i2c`.
ESP-IDF 5.5.5 and 6.1 builds have been checked for target `esp32`.

From the repository root, using ESP-IDF 5.5.5 (the most recently tested board
setup):

```sh
source "$HOME/.espressif/tools/activate_idf_v5.5.5.sh"
cd wroom_firmware
export IDF_TARGET=esp32
idf.py build
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

Use the actual WROOM serial port if its name changes. Exit the monitor with
Ctrl+]. The explicit environment target overrides any `IDF_TARGET=esp32s3`
inherited from the touchscreen setup; `-D IDF_TARGET=esp32` alone does not
override that environment check. When returning to the touchscreen project,
use `export IDF_TARGET=esp32s3` in its shell.

To use the installed 6.1 setup instead, run from `wroom_firmware/`:

```sh
source "$HOME/.espressif/tools/activate_idf_v6.1.sh"
export IDF_TARGET=esp32
idf.py -B build/idf61 -D SDKCONFIG=build/idf61/sdkconfig -D IDF_TARGET=esp32 build
idf.py -B build/idf61 -p /dev/cu.usbserial-0001 flash monitor
```

This separate build/configuration directory preserves the 5.5.5 build.
Only one monitor can use the serial port at a time. Close any existing monitor
with Ctrl+] before flashing; a port-lock error does not mean compilation failed.

## Transport and hardware checks

I2C commands carry the PN532 frame directly. Reads consume the status byte
and complete ACK/response in one transaction, test the RDY bit, and validate
ACK bytes, length, direction, response command, checksum and postamble.
Responses use the existing 64-byte packet capacity; oversized/extended frames
are rejected. The complete response is cached for the existing command API.
I2C transfers have a 100 ms limit, address NACKs have bounded retries, and
ACK/response waits honor the command timeout (zero uses a finite 1 second
default). A timed-out scan is aborted so a subsequent scan can proceed.
The optional SPI transport remains available; the active NFC path uses I2C.

Protocol reference: [NXP PN532 manual, sections 6.2.1, 6.2.2 and 6.2.4](https://www.nxp.com/docs/en/user-guide/141520.pdf).
Driver reference: [ESP-IDF 6.1 I2C master](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/i2c.html).

After flashing, check:

1. `PN532 detected!`, `SAM configured successfully`, and `NFC ready` appear.
2. Player 1 UID `B3 C9 B6 1D` logs `Player 1 detected`, sends `PLAYER:1`,
   and plays the success sound.
3. An unknown fob logs its UID and `Unknown NFC fob`, sends `NFC:UNKNOWN`,
   and plays the error sound.
4. The touchscreen reflects both scans. `WROOM:READY`/`S3:READY` are unchanged.
5. With no fob present, the controller stays responsive; present a fob again
   to confirm scanning resumes. The existing 1500 ms repeat-scan delay remains.

If detection fails, check module I2C selection after a power cycle, power,
common ground, SDA/SCL wiring and pull-ups. Startup detection has intermittently
failed after resets; this is not confirmed fixed. With the monitor open, press
the WROOM EN/RESET button once and check for `NFC ready` before scanning.
If needed, power-cycle both the WROOM and PN532. To attach without requesting
another reset, use `idf.py -p /dev/cu.usbserial-0001 monitor --no-reset`
(add `-B build/idf61` for the 6.1 build). This only attaches the monitor; it
does not restart an application that already exited after initialization failed.

Audio diagnostics log the DAC pin, sample rate, queued byte count and elapsed
write time. Writes use a finite timeout and report driver errors or short writes.
A successful queue log confirms data submission, not audible output; verify
both sounds on the physical speaker.

The observed boot warning about
4 MB physical flash versus a 2 MB image comes from the existing flash setting;
this migration does not change it.
