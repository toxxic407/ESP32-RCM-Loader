#include <Arduino.h>
#include <vector>
#include <usb_msc_disk.h>
#include <EspUsbHost.h>

#include "RCM.h"


static constexpr uint8_t BOOT_BUTTON_PIN = 0;
static constexpr uint32_t MSC_BUTTON_WINDOW_MS = 3000;
static constexpr uint32_t MSC_BUTTON_DEBOUNCE_MS = 20;

//White = Plug in your Switch in RCM Mode
//Yellow = executing Exploit
//Red = Something went wrong
//Blue = USB Storage Mode to transfer payloads

static int brightness = 20; // LED brightness level (0-255)
std::vector<uint8_t> payload;

void rcm() {
  //Detect Switch in RCM Mode
  Serial0.println("Detecting Device...");
  RCM.detect_device();
  rgbLedWrite(RGB_BUILTIN, brightness, brightness, 0);

  //Patching Payload
  Serial0.println("Patching Payload");
  std::vector<uint8_t> patched_payload = RCM.create_rcm_message(payload);
  if(patched_payload.size() == 0){
    Serial0.println("Failed to create RCM message");
    rgbLedWrite(RGB_BUILTIN, brightness, 0, 0);
    return;
  };

  //Upload Payload to Switch
  Serial0.println("Uploading RCM message...");
  if(!RCM.write(patched_payload)){
    Serial0.println("Failed to upload RCM message");
    rgbLedWrite(RGB_BUILTIN, brightness, 0, 0);
    return;
  }

  //Switch to High Buffer
  Serial0.println("Switching to high buffer...");
  if (!RCM.switch_to_highbuf()) {
    Serial0.println("Failed to switch to high buffer");
    rgbLedWrite(RGB_BUILTIN, brightness, 0, 0);
    return;
  }

  //Smashing Stack
  Serial0.println("Triggering controlled memcpy...");
  if (!RCM.trigger_controlled_memcpy()) {
    Serial0.println("Failed to trigger controlled memcpy");
    rgbLedWrite(RGB_BUILTIN, brightness, 0, 0);
    return;
  }

  Serial0.println("Stack has been smashed! Payload should be running on the Switch now.");
  rgbLedWrite(RGB_BUILTIN, 0, brightness, 0);
  delay(3000);
  Serial0.println("Restarting device...");
  ESP.restart();
}

static bool bootButtonPressedDuringStartup() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  const uint32_t start = millis();
  bool stablePressed = false;
  uint32_t lowSince = 0;

  while (millis() - start < MSC_BUTTON_WINDOW_MS) {
    const bool currentlyPressed = digitalRead(BOOT_BUTTON_PIN) == LOW;

    if (currentlyPressed) {
      if (lowSince == 0) {
        lowSince = millis();
      } else if (!stablePressed && millis() - lowSince >= MSC_BUTTON_DEBOUNCE_MS) {
        stablePressed = true;
      }
    } else {
      lowSince = 0;
    }

    if (stablePressed) {
      return true;
    }

    delay(10);
  }

  return stablePressed;
}

void initUsbMscDisk() {
  if (USBMscDisk.begin()) {
      rgbLedWrite(RGB_BUILTIN, 0, 0, brightness);
    } else {
      Serial0.println("USB MSC initialization failed");
      rgbLedWrite(RGB_BUILTIN, brightness, 0, 0);
    }
}

void setup() {
  Serial.setTxBufferSize(4096);
  Serial0.begin(115200);

  if (bootButtonPressedDuringStartup()) {
    Serial0.println("BOOT button detected; initializing USB MSC disk...");
    initUsbMscDisk();
  } else {
    rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
    Serial0.println("Normal application mode");
    delay(1000); // Wait for Serial to initialize
    Serial0.println("BOOT button not pressed; skipping USB MSC initialization");

    //std::vector<uint8_t> payload;
    if (USBMscDisk.readRootFile("PAYLOAD.BIN", payload)) {
      Serial0.printf("Loaded PAYLOAD.BIN: %u bytes\n", (unsigned)payload.size());

      if (RCM.begin()) {
        rgbLedWrite(RGB_BUILTIN, brightness, brightness, brightness);
        Serial0.println("USB Host mode started");
        rcm();
      } else {
        rgbLedWrite(RGB_BUILTIN, brightness, 0, 0);
        Serial0.println("USB Host mode failed to start");
      }
    } else {
      Serial0.println("PAYLOAD.BIN not found or could not be read, starting USB Mode for file transfer...");
      initUsbMscDisk();
    }
  }
}

void loop() {
}