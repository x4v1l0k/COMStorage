#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class UiStatus : uint8_t {
  Booting = 0,
  Ready,
  Failure,
  Transferring,
  Busy,
  Disconnected,
};

class StatusDisplay {
 public:
  bool begin();
  void setStatus(UiStatus status);
  void setStorage(uint64_t total, uint64_t used, uint64_t freeBytes);
  void setBackend(const char *backend);
  void refresh();  // full redraw
  UiStatus status() const { return status_; }

 private:
  bool ready_ = false;
  UiStatus status_ = UiStatus::Booting;
  const char *backend_ = "—";
  uint64_t total_ = 0;
  uint64_t used_ = 0;
  uint64_t free_ = 0;

  const char *statusText() const;
  uint16_t statusColor() const;
  void formatBytes(uint64_t bytes, char *out, size_t outLen) const;
};
