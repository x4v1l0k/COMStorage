#include <Arduino.h>
#include "board_config.h"
#include "sd_backend.h"
#include "protocol.h"
#include "status_display.h"

static void apa102Set(uint8_t r, uint8_t g, uint8_t b) {
  auto writeByte = [](uint8_t v) {
    for (int i = 7; i >= 0; --i) {
      digitalWrite(PIN_LED_DATA, (v >> i) & 1);
      digitalWrite(PIN_LED_CLOCK, HIGH);
      delayMicroseconds(1);
      digitalWrite(PIN_LED_CLOCK, LOW);
      delayMicroseconds(1);
    }
  };
  for (int i = 0; i < 4; i++) {
    writeByte(0x00);
  }
  writeByte(0xE0 | 0x1F);
  writeByte(b);
  writeByte(g);
  writeByte(r);
  for (int i = 0; i < 4; i++) {
    writeByte(0xFF);
  }
}

static SDBackend gSd;
static StorageBackend *gStorage = nullptr;
static ProtocolServer *gServer = nullptr;
static StatusDisplay gDisplay;

void setup() {
  pinMode(PIN_LED_DATA, OUTPUT);
  pinMode(PIN_LED_CLOCK, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_LED_DATA, LOW);
  digitalWrite(PIN_LED_CLOCK, LOW);
  apa102Set(0, 0, 32);

  gDisplay.begin();
  gDisplay.setStatus(UiStatus::Booting);
  gDisplay.setBackend("SD");

  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("{\"event\":\"log\",\"msg\":\"COMStorage firmware starting\"}");
  Serial.println("{\"event\":\"log\",\"msg\":\"USB MSC disabled by design\"}");
  Serial.println("{\"event\":\"log\",\"msg\":\"primary storage is SD card (SDMMC)\"}");

  // PoC requires the inserted microSD — not onboard LittleFS flash.
  if (!gSd.begin()) {
    Serial.println("{\"event\":\"error\",\"msg\":\"SD mount failed — insert FAT/FAT32 card\"}");
    apa102Set(64, 0, 0);
    gDisplay.setStatus(UiStatus::Failure);
    gDisplay.setBackend("NO-SD");
    gDisplay.setStorage(0, 0, 0);
    // Keep CDC alive so host can still see the COM port and diagnose.
    static ProtocolServer server(&gSd, &gDisplay);
    gServer = &server;
    gServer->begin();
    return;
  }

  gStorage = &gSd;
  const StorageInfo si = gStorage->info();
  gDisplay.setBackend(si.backend);
  gDisplay.setStorage(si.total_bytes, si.used_bytes, si.free_bytes);

  static ProtocolServer server(gStorage, &gDisplay);
  gServer = &server;
  gServer->begin();

  Serial.printf(
      "{\"event\":\"storage\",\"backend\":\"%s\",\"total\":%llu,\"used\":%llu,\"free\":%llu}\n",
      si.backend, (unsigned long long)si.total_bytes, (unsigned long long)si.used_bytes,
      (unsigned long long)si.free_bytes);

  apa102Set(0, 48, 0);
}

void loop() {
  static bool wasConnected = true;
  const bool connected = Serial;
  if (wasConnected && !connected && gServer) {
    gServer->onUsbDisconnect();
  }
  if (!wasConnected && connected && gServer && gSd.isMounted()) {
    gDisplay.setStatus(UiStatus::Ready);
  }
  wasConnected = connected;

  if (gServer) {
    gServer->loop();
  }

  static bool lastBtn = true;
  const bool btn = digitalRead(PIN_BUTTON);
  if (lastBtn && !btn) {
    apa102Set(48, 48, 0);
    if (gSd.isMounted()) {
      const StorageInfo si = gSd.info();
      gDisplay.setStorage(si.total_bytes, si.used_bytes, si.free_bytes);
    }
    delay(80);
    apa102Set(gSd.isMounted() ? 0 : 64, gSd.isMounted() ? 48 : 0, 0);
  }
  lastBtn = btn;
}
