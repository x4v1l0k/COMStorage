#include "status_display.h"
#include "board_config.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

namespace {

// Official LilyGO factory: SPI2_HOST, backlight active-LOW, landscape gap (1,26).
class DongleTFT : public Adafruit_ST7735 {
 public:
  DongleTFT(SPIClass *spi, int8_t cs, int8_t dc, int8_t rst)
      : Adafruit_ST7735(spi, cs, dc, rst) {}

  void applyOffsets(int8_t col, int8_t row) { setColRowStart(col, row); }
};

SPIClass *gTftSpi = nullptr;
DongleTFT *gTft = nullptr;

// Portrait panel offsets; after setRotation(1) Adafruit maps to x=1,y=26 (factory landscape).
static const int8_t kColStart = 26;
static const int8_t kRowStart = 1;

}  // namespace

bool StatusDisplay::begin() {
  // Backlight OFF while initializing (active-LOW on T-Dongle-S3).
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  // SPI2_HOST == FSPI on ESP32-S3 (same bus LilyGO factory example uses).
  gTftSpi = new SPIClass(FSPI);
  gTftSpi->begin(PIN_TFT_SCLK, -1 /* MISO */, PIN_TFT_MOSI, PIN_TFT_CS);

  gTft = new DongleTFT(gTftSpi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
  gTft->initR(INITR_MINI160x80);
  gTft->applyOffsets(kColStart, kRowStart);
  gTft->setRotation(3);  // landscape 160x80, 180° vs rotation 1
  gTft->invertDisplay(true);
  gTft->fillScreen(ST77XX_BLACK);

  // Backlight ON (active-LOW). Factory uses ledcWrite(..., 0) for full brightness.
  digitalWrite(PIN_TFT_BL, LOW);

  ready_ = true;
  refresh();
  return true;
}

const char *StatusDisplay::statusText() const {
  switch (status_) {
    case UiStatus::Booting:
      return "Booting";
    case UiStatus::Ready:
      return "Ready";
    case UiStatus::Failure:
      return "Failure";
    case UiStatus::Transferring:
      return "Transferring";
    case UiStatus::Busy:
      return "Busy";
    case UiStatus::Disconnected:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

uint16_t StatusDisplay::statusColor() const {
  switch (status_) {
    case UiStatus::Ready:
      return ST77XX_GREEN;
    case UiStatus::Failure:
      return ST77XX_RED;
    case UiStatus::Transferring:
      return ST77XX_YELLOW;
    case UiStatus::Busy:
      return ST77XX_CYAN;
    case UiStatus::Disconnected:
      return ST77XX_ORANGE;
    case UiStatus::Booting:
    default:
      return ST77XX_WHITE;
  }
}

void StatusDisplay::formatBytes(uint64_t bytes, char *out, size_t outLen) const {
  if (bytes >= (1024ULL * 1024ULL)) {
    const double mb = (double)bytes / (1024.0 * 1024.0);
    snprintf(out, outLen, "%.2f MB", mb);
  } else if (bytes >= 1024ULL) {
    const double kb = (double)bytes / 1024.0;
    snprintf(out, outLen, "%.1f KB", kb);
  } else {
    snprintf(out, outLen, "%llu B", (unsigned long long)bytes);
  }
}

void StatusDisplay::setStatus(UiStatus status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  refresh();
}

void StatusDisplay::setBackend(const char *backend) {
  backend_ = backend ? backend : "-";
  refresh();
}

void StatusDisplay::setStorage(uint64_t total, uint64_t used, uint64_t freeBytes) {
  total_ = total;
  used_ = used;
  free_ = freeBytes;
  refresh();
}

void StatusDisplay::refresh() {
  if (!ready_ || !gTft) {
    return;
  }

  DongleTFT &tft = *gTft;
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextWrap(false);
  tft.setCursor(4, 4);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.print("COMStorage");

  tft.setCursor(100, 4);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(backend_);

  tft.setCursor(4, 18);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Status: ");
  tft.setTextColor(statusColor());
  tft.print(statusText());

  tft.drawFastHLine(0, 30, 160, 0x4208);

  char totalBuf[24];
  char usedBuf[24];
  char freeBuf[24];
  formatBytes(total_, totalBuf, sizeof(totalBuf));
  formatBytes(used_, usedBuf, sizeof(usedBuf));
  formatBytes(free_, freeBuf, sizeof(freeBuf));

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 36);
  tft.print("Total: ");
  tft.print(totalBuf);

  tft.setCursor(4, 48);
  tft.print("Used:  ");
  tft.print(usedBuf);

  tft.setCursor(4, 60);
  tft.print("Free:  ");
  tft.print(freeBuf);

  tft.drawRect(4, 72, 152, 6, ST77XX_WHITE);
  if (total_ > 0) {
    uint16_t fill = (uint16_t)((used_ * 150ULL) / total_);
    if (fill > 150) {
      fill = 150;
    }
    const uint16_t barColor = (used_ * 100ULL / total_ > 90) ? ST77XX_RED : ST77XX_GREEN;
    if (fill > 0) {
      tft.fillRect(5, 73, fill, 4, barColor);
    }
  }
}
