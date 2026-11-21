# ESP32-S3 Switch RCM Payload Injector

Firmware for the **ESP32-S3-DevKitC-1 (N16R8V)** that injects RCM payloads into a Nintendo Switch over USB Host and exposes a USB Mass Storage (`USB MSC`) drive so you can copy or update `PAYLOAD.BIN` directly from your computer.

## What It Does

* **Payload Injection:** On normal boot with `PAYLOAD.BIN` present, the ESP32-S3 switches to USB Host mode, waits for a Nintendo Switch in RCM mode, patches and uploads the payload, triggers the exploit, and then resets itself for further injections
* **USB Mass Storage Mode (`BOOT` button):** Hold the `BOOT` button (`GPIO0`) during startup to mount the board as a USB mass storage drive and place/update `PAYLOAD.BIN` in the root directory.
* **Automatic Fallback to M Mode:** If `PAYLOAD.BIN` is missing or cannot be read on normal boot, the board automatically enters USB MSC mode so you can transfer the file.
* **RGB Status Feedback:** Uses the LED to indicate current status:
  * **White:** USB Host ready — connect Switch in RCM mode
  * **Yellow:** Executing RCM exploit / uploading payload
  * **Green:** Payload injected
  * **Blue:** USB Mass Storage mode active (`PAYLOAD.BIN` transfer)
  * **Red:** Error

## Setup

1. Install the Firmware and partition table
2. Connect the ESP via its USB Port to a Computer (Should appear as `RCM-JIG` with 1MB Space)
4. Transfer the Payload you want to use onto it and name it `payload.bin` or `PAYLOAD.BIN`
5. PRESS EJECT SAFELY!
6. Disconnect the Device
7. Everything should be set up now.

## Usage

1. Power the ESP via the `COM` Port without pressing Buttons
2. The ESPs LED should turn white after a couple Seconds. If its Blue instead either `BOOT` was pressed during Startup or the Payload cant be found (Setup either wasntdone or not done properly)
3. Put your Switch into RCM Mode
4. Plug your Switch into the ESPs `ÙSB` Port via USB-C to USB-C Cable
5. The ESPs LED should now turn Yellow. If it stays white the Switch is either not in RCM Mode or not connected properly
6. The ESPs LED should turn green when the Exploit has finished
7. The Switch should now be launching the Payload

## Changing Payloads

1. Connect the ESP via its USB Port to a Computer and press `BOOT` once immediately after plugging it in
2. The ESPs LED should turn Blue. if it turns white instead press `RST` and then `BOOT` immediately after
3. The ESP should appear as `RCM-JIG` with 1MB Space in Explorer
4. Transfer the Payload you want to use onto it and name it `payload.bin` or `PAYLOAD.BIN`
5. PRESS EJECT SAFELY!
6. Disconnect the Device
