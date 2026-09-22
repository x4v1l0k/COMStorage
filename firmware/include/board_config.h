#pragma once

// LILYGO TTGO T-Dongle-S3 board pin map (official wiki / examples).
// USB: ESP32-S3 native USB (USB-OTG PHY) via Type-A male plug.
// This firmware uses Hardware CDC only — no USB MSC.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <Arduino.h>

// --- MCU ---
// ESP32-S3, 16 MB Quad-SPI flash, 512 KB SRAM, no PSRAM

// --- APA102 RGB LED ---
static const int PIN_LED_DATA = 40;
static const int PIN_LED_CLOCK = 39;

// --- BOOT button ---
static const int PIN_BUTTON = 0;

// --- ST7735 0.96" 160x80 (not driven in this PoC build; pins documented) ---
static const int PIN_TFT_CS = 4;
static const int PIN_TFT_MOSI = 3;
static const int PIN_TFT_SCLK = 5;
static const int PIN_TFT_DC = 2;
static const int PIN_TFT_RST = 1;
static const int PIN_TFT_BL = 38;

// --- TF / microSD (SDMMC) — present on board; optional backend ---
static const int PIN_SD_D0 = 14;
static const int PIN_SD_D1 = 17;
static const int PIN_SD_D2 = 21;
static const int PIN_SD_D3 = 18;
static const int PIN_SD_CLK = 12;
static const int PIN_SD_CMD = 16;

// --- QWIIC (default UART function on official docs) ---
static const int PIN_QWIIC_TX = 43;
static const int PIN_QWIIC_RX = 44;

#ifndef COMSTORAGE_DEVICE_NAME
#define COMSTORAGE_DEVICE_NAME "TTGO-T-Dongle-S3"
#endif

#ifndef COMSTORAGE_PROTOCOL_VERSION
#define COMSTORAGE_PROTOCOL_VERSION "1.0"
#endif

#ifndef COMSTORAGE_MAX_PATH
#define COMSTORAGE_MAX_PATH 200
#endif

#ifndef COMSTORAGE_MAX_FILE_SIZE
#define COMSTORAGE_MAX_FILE_SIZE (12 * 1024 * 1024)
#endif

#ifndef COMSTORAGE_CHUNK_SIZE
#define COMSTORAGE_CHUNK_SIZE 4096
#endif

#ifndef COMSTORAGE_CMD_TIMEOUT_MS
#define COMSTORAGE_CMD_TIMEOUT_MS 30000
#endif

#ifndef COMSTORAGE_RX_LINE_MAX
#define COMSTORAGE_RX_LINE_MAX 512
#endif

#endif
