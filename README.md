# SparkFun ESP32-C6 SD Card + Wi-Fi Config Portal

This project provides an Arduino sketch (`.ino`) for a SparkFun ESP32-C6 board that:

- Loads saved Wi-Fi credentials from NVS (`Preferences`).
- Falls back to **AP mode** when credentials are missing or connection fails.
- Serves a **web UI from LittleFS**.
- Lets users configure Wi-Fi in-browser.
- Lets users browse an SD card, upload files, download files, create folders, and delete entries.

## Files

- `esp32_sdcard_manager.ino` – firmware sketch.
- `data/index.html` – web UI to upload to LittleFS.

## Arduino IDE setup

1. Install ESP32 board package (Espressif) in Arduino IDE.
2. Select board: **SparkFun ESP32-C6 Thing Plus** (or equivalent ESP32-C6 target).
3. Install required libraries (if not bundled by core):
   - `LittleFS`
   - `SD`
   - `Preferences`
4. Ensure you have a LittleFS upload tool available for your IDE (plugin/tooling varies by IDE version).

## Pin configuration

Current sketch defaults:

- `SD_CS_PIN = 10`
- `SD_SCK_PIN = 6`
- `SD_MISO_PIN = 5`
- `SD_MOSI_PIN = 4`

If your wiring differs, edit pin constants near the top of `esp32_sdcard_manager.ino`.

## Build and flash

1. Open `esp32_sdcard_manager.ino`.
2. Build and upload the sketch.
3. Upload LittleFS data (the `data/` folder containing `index.html`).
4. Open Serial Monitor at `115200` baud.

## First boot behavior

- If no Wi-Fi credentials are stored, device starts an access point:
  - SSID: `ESP32C6-SD-xxxx`
  - Password: `esp32config`
- Connect to the AP and open `http://192.168.4.1/`.

## Web API (used by UI)

- `GET /api/status` – current mode/IP.
- `GET /api/wifi` – stored SSID.
- `POST /api/wifi` – save SSID/password and reboot.
- `GET /api/scan` – scan nearby APs.
- `GET /api/files?path=/` – list directory.
- `POST /api/upload?path=/folder` – upload file (multipart form).
- `GET /api/download?path=/file.txt` – download file.
- `POST /api/mkdir` with `path=/newdir` – create folder.
- `DELETE /api/file?path=/target` – delete file/folder.

## Notes

- Directory deletion requires directory to be empty.
- Basic path traversal checks are implemented (`..` is rejected).
- For production, consider adding authentication and HTTPS (or isolate on trusted network).
