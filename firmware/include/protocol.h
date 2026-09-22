#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage_backend.h"
#include "status_display.h"

class ProtocolServer {
 public:
  ProtocolServer(StorageBackend *storage, StatusDisplay *display);

  void begin();
  void loop();
  void onUsbDisconnect();

 private:
  StorageBackend *storage_;
  StatusDisplay *display_;
  String lineBuf_;
  bool transferActive_ = false;

  void handleLine(const String &line);
  void sendJson(JsonDocument &doc);
  void sendError(uint32_t requestId, const char *code, const char *message);
  void uiReady();
  void uiBusy();
  void uiTransfer();
  void uiFailure();
  void refreshStorageUi();

  void cmdInfo(JsonDocument &req);
  void cmdStorage(JsonDocument &req);
  void cmdList(JsonDocument &req);
  void cmdStat(JsonDocument &req);
  void cmdDelete(JsonDocument &req);
  void cmdMkdir(JsonDocument &req);
  void cmdRename(JsonDocument &req);
  void cmdTouch(JsonDocument &req);
  void cmdChmod(JsonDocument &req);
  void cmdFormat(JsonDocument &req);
  void cmdGet(JsonDocument &req);
  void cmdPut(JsonDocument &req);
  void cmdHash(JsonDocument &req);
  void cmdPing(JsonDocument &req);

  bool requireMounted(uint32_t requestId);

  bool resolvePath(JsonDocument &req, const char *field, char *out, size_t outLen,
                   uint32_t requestId);
  bool resolvePath(JsonDocument &req, char *out, size_t outLen, uint32_t requestId);
  bool readExact(uint8_t *buf, size_t len, uint32_t timeoutMs);
  bool writeAll(const uint8_t *buf, size_t len);
  uint32_t requestIdOf(JsonDocument &req);
};
