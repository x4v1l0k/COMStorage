#include "protocol.h"
#include "board_config.h"
#include "path_util.h"
#include "crc32.h"
#include <esp_system.h>

namespace {

struct ListCtx {
  JsonArray arr;
  size_t count;
  size_t maxEntries;
};

bool listCollect(const char *name, bool isDir, uint64_t size, void *ctx) {
  ListCtx *c = static_cast<ListCtx *>(ctx);
  if (c->count >= c->maxEntries) {
    return false;
  }
  JsonObject o = c->arr.add<JsonObject>();
  o["name"] = name;
  o["type"] = isDir ? "dir" : "file";
  o["size"] = size;
  c->count++;
  return true;
}

void writeU32LE(uint8_t *dst, uint32_t v) {
  dst[0] = (uint8_t)(v & 0xFF);
  dst[1] = (uint8_t)((v >> 8) & 0xFF);
  dst[2] = (uint8_t)((v >> 16) & 0xFF);
  dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t readU32LE(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

}  // namespace

ProtocolServer::ProtocolServer(StorageBackend *storage, StatusDisplay *display)
    : storage_(storage), display_(display) {}

void ProtocolServer::begin() {
  lineBuf_.reserve(COMSTORAGE_RX_LINE_MAX);
  lineBuf_ = "";
  transferActive_ = false;
  refreshStorageUi();
  uiReady();
  Serial.println();
  Serial.println("{\"event\":\"boot\",\"device\":\"" COMSTORAGE_DEVICE_NAME
                 "\",\"protocol_version\":\"" COMSTORAGE_PROTOCOL_VERSION
                 "\",\"transport\":\"USB-CDC\"}");
}

void ProtocolServer::onUsbDisconnect() {
  lineBuf_ = "";
  transferActive_ = false;
  if (display_) {
    display_->setStatus(UiStatus::Disconnected);
  }
}

void ProtocolServer::uiReady() {
  if (display_) {
    display_->setStatus(UiStatus::Ready);
  }
}

void ProtocolServer::uiBusy() {
  if (display_) {
    display_->setStatus(UiStatus::Busy);
  }
}

void ProtocolServer::uiTransfer() {
  if (display_) {
    display_->setStatus(UiStatus::Transferring);
  }
}

void ProtocolServer::uiFailure() {
  if (display_) {
    display_->setStatus(UiStatus::Failure);
  }
}

void ProtocolServer::refreshStorageUi() {
  if (!display_ || !storage_) {
    return;
  }
  const StorageInfo si = storage_->info();
  display_->setBackend(si.backend);
  display_->setStorage(si.total_bytes, si.used_bytes, si.free_bytes);
}

void ProtocolServer::sendJson(JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.write('\n');
  Serial.flush();
}

uint32_t ProtocolServer::requestIdOf(JsonDocument &req) {
  if (req["request_id"].is<uint32_t>()) {
    return req["request_id"].as<uint32_t>();
  }
  return 0;
}

void ProtocolServer::sendError(uint32_t requestId, const char *code, const char *message) {
  JsonDocument doc;
  doc["status"] = "error";
  doc["request_id"] = requestId;
  doc["error"] = code;
  doc["message"] = message;
  sendJson(doc);
}

bool ProtocolServer::resolvePath(JsonDocument &req, const char *field, char *out, size_t outLen,
                                 uint32_t requestId) {
  if (!req[field].is<const char *>()) {
    sendError(requestId, "bad_request", "missing path field");
    return false;
  }
  const char *path = req[field];
  if (!normalizePath(path, out, outLen)) {
    sendError(requestId, "invalid_path", "path rejected by sanitizer");
    return false;
  }
  return true;
}

bool ProtocolServer::resolvePath(JsonDocument &req, char *out, size_t outLen, uint32_t requestId) {
  return resolvePath(req, "path", out, outLen, requestId);
}

bool ProtocolServer::readExact(uint8_t *buf, size_t len, uint32_t timeoutMs) {
  size_t got = 0;
  const uint32_t start = millis();
  while (got < len) {
    if (millis() - start > timeoutMs) {
      return false;
    }
    const int avail = Serial.available();
    if (avail <= 0) {
      delay(1);
      yield();
      continue;
    }
    const size_t n = Serial.readBytes(reinterpret_cast<char *>(buf + got), len - got);
    got += n;
  }
  return true;
}

bool ProtocolServer::writeAll(const uint8_t *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const size_t n = Serial.write(buf + sent, len - sent);
    if (n == 0) {
      delay(1);
      yield();
      continue;
    }
    sent += n;
  }
  return true;
}

void ProtocolServer::loop() {
  while (Serial.available() > 0 && !transferActive_) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (lineBuf_.length() > 0) {
        handleLine(lineBuf_);
        lineBuf_ = "";
      }
      continue;
    }
    if (lineBuf_.length() >= COMSTORAGE_RX_LINE_MAX) {
      lineBuf_ = "";
      sendError(0, "line_too_long", "command line exceeded limit");
      // Drain until newline
      while (Serial.available()) {
        const int d = Serial.read();
        if (d == '\n') {
          break;
        }
      }
      continue;
    }
    lineBuf_ += (char)c;
  }
}

void ProtocolServer::handleLine(const String &line) {
  JsonDocument req;
  const DeserializationError err = deserializeJson(req, line);
  if (err) {
    sendError(0, "bad_json", err.c_str());
    return;
  }
  if (!req["cmd"].is<const char *>()) {
    sendError(requestIdOf(req), "bad_request", "missing cmd");
    return;
  }

  const char *cmd = req["cmd"];
  {
    JsonDocument logDoc;
    logDoc["event"] = "log";
    logDoc["msg"] = "cmd";
    logDoc["cmd"] = cmd;
    logDoc["request_id"] = requestIdOf(req);
    sendJson(logDoc);
  }

  if (strcmp(cmd, "ping") == 0) {
    cmdPing(req);
  } else if (strcmp(cmd, "info") == 0) {
    cmdInfo(req);
  } else if (strcmp(cmd, "storage") == 0) {
    cmdStorage(req);
  } else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) {
    cmdList(req);
  } else if (strcmp(cmd, "stat") == 0) {
    cmdStat(req);
  } else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "rm") == 0) {
    cmdDelete(req);
  } else if (strcmp(cmd, "mkdir") == 0) {
    cmdMkdir(req);
  } else if (strcmp(cmd, "rename") == 0 || strcmp(cmd, "move") == 0 || strcmp(cmd, "mv") == 0) {
    cmdRename(req);
  } else if (strcmp(cmd, "touch") == 0) {
    cmdTouch(req);
  } else if (strcmp(cmd, "chmod") == 0) {
    cmdChmod(req);
  } else if (strcmp(cmd, "format") == 0) {
    cmdFormat(req);
  } else if (strcmp(cmd, "get") == 0) {
    cmdGet(req);
  } else if (strcmp(cmd, "put") == 0) {
    cmdPut(req);
  } else if (strcmp(cmd, "hash") == 0) {
    cmdHash(req);
  } else {
    sendError(requestIdOf(req), "unknown_cmd", "command not supported");
  }
}

void ProtocolServer::cmdPing(JsonDocument &req) {
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = requestIdOf(req);
  doc["pong"] = true;
  sendJson(doc);
}

void ProtocolServer::cmdInfo(JsonDocument &req) {
  const StorageInfo si = storage_->info();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = requestIdOf(req);
  doc["device"] = COMSTORAGE_DEVICE_NAME;
  doc["protocol_version"] = COMSTORAGE_PROTOCOL_VERSION;
  doc["transport"] = "USB-CDC";
  doc["usb_class"] = "CDC";
  doc["storage"] = si.backend;
  doc["storage_mounted"] = si.mounted;
  doc["chip"] = "ESP32-S3";
  doc["flash_size"] = ESP.getFlashChipSize();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["sdk"] = ESP.getSdkVersion();
  doc["max_file_size"] = COMSTORAGE_MAX_FILE_SIZE;
  doc["chunk_size"] = COMSTORAGE_CHUNK_SIZE;
  doc["format_supported"] = storage_->supportsFormat();
  doc["format_filesystems"] = "fat32";
  doc["format_note"] = "NTFS not supported on ESP32; FAT32 only";
  sendJson(doc);
}

void ProtocolServer::cmdStorage(JsonDocument &req) {
  const StorageInfo si = storage_->info();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = requestIdOf(req);
  doc["backend"] = si.backend;
  doc["mounted"] = si.mounted;
  doc["total_bytes"] = si.total_bytes;
  doc["used_bytes"] = si.used_bytes;
  doc["free_bytes"] = si.free_bytes;
  sendJson(doc);
}

void ProtocolServer::cmdList(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }

  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = path;
  JsonArray entries = doc["entries"].to<JsonArray>();
  ListCtx ctx{entries, 0, 256};
  if (!storage_->list(path, listCollect, &ctx)) {
    sendError(rid, "list_failed", "cannot list path");
    return;
  }
  doc["count"] = ctx.count;
  sendJson(doc);
}

void ProtocolServer::cmdStat(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  FileMeta meta{};
  if (!storage_->stat(path, meta)) {
    sendError(rid, "stat_failed", "stat error");
    return;
  }
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = path;
  doc["exists"] = meta.exists;
  if (meta.exists) {
    doc["type"] = meta.is_dir ? "dir" : "file";
    doc["size"] = meta.size;
  }
  sendJson(doc);
}

void ProtocolServer::cmdDelete(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  if (strcmp(path, "/") == 0) {
    sendError(rid, "forbidden", "cannot delete root");
    return;
  }
  if (!storage_->exists(path)) {
    sendError(rid, "not_found", "path does not exist");
    return;
  }
  uiBusy();
  if (!storage_->remove(path)) {
    uiFailure();
    sendError(rid, "delete_failed", "remove failed");
    return;
  }
  refreshStorageUi();
  uiReady();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = path;
  doc["deleted"] = true;
  sendJson(doc);
}

void ProtocolServer::cmdMkdir(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  if (strcmp(path, "/") == 0) {
    sendError(rid, "forbidden", "root already exists");
    return;
  }
  uiBusy();
  if (!storage_->mkdir(path)) {
    uiFailure();
    sendError(rid, "mkdir_failed", "mkdir failed");
    return;
  }
  refreshStorageUi();
  uiReady();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = path;
  sendJson(doc);
}

void ProtocolServer::cmdRename(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char from[COMSTORAGE_MAX_PATH + 1];
  char to[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, "path", from, sizeof(from), rid)) {
    return;
  }
  // Accept new_path or to
  const char *field = req["new_path"].is<const char *>() ? "new_path" : "to";
  if (!resolvePath(req, field, to, sizeof(to), rid)) {
    return;
  }
  if (strcmp(from, "/") == 0 || strcmp(to, "/") == 0) {
    sendError(rid, "forbidden", "cannot rename root");
    return;
  }
  if (!storage_->exists(from)) {
    sendError(rid, "not_found", "source path does not exist");
    return;
  }
  uiBusy();
  if (!storage_->rename(from, to)) {
    uiFailure();
    sendError(rid, "rename_failed", "rename/move failed");
    return;
  }
  refreshStorageUi();
  uiReady();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = from;
  doc["new_path"] = to;
  sendJson(doc);
}

void ProtocolServer::cmdTouch(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  if (strcmp(path, "/") == 0) {
    sendError(rid, "forbidden", "cannot create root as file");
    return;
  }
  uiBusy();
  if (storage_->exists(path)) {
    refreshStorageUi();
    uiReady();
    JsonDocument doc;
    doc["status"] = "ok";
    doc["request_id"] = rid;
    doc["path"] = path;
    doc["created"] = false;
    sendJson(doc);
    return;
  }
  File f = storage_->openWrite(path, true);
  if (!f) {
    uiFailure();
    sendError(rid, "touch_failed", "cannot create file");
    return;
  }
  f.close();
  refreshStorageUi();
  uiReady();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = path;
  doc["created"] = true;
  sendJson(doc);
}

void ProtocolServer::cmdChmod(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  // LittleFS / FAT on this PoC have no POSIX permission bits.
  JsonDocument doc;
  doc["status"] = "error";
  doc["request_id"] = rid;
  doc["error"] = "not_supported";
  doc["message"] = "filesystem backend has no POSIX permissions";
  doc["permissions_supported"] = false;
  sendJson(doc);
}

bool ProtocolServer::requireMounted(uint32_t requestId) {
  if (storage_ && storage_->isMounted()) {
    return true;
  }
  sendError(requestId, "not_mounted", "SD card not mounted — insert a FAT/FAT32 card");
  uiFailure();
  return false;
}

void ProtocolServer::cmdFormat(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);

  if (!req["confirm"].is<const char *>() || strcmp(req["confirm"], "yes") != 0) {
    sendError(rid, "confirm_required", "destructive format requires confirm=\"yes\"");
    return;
  }
  if (!req["filesystem"].is<const char *>()) {
    sendError(rid, "bad_request", "missing filesystem (fat32|ntfs)");
    return;
  }

  const char *fs = req["filesystem"];
  uiBusy();
  char err[192];
  err[0] = '\0';
  if (!storage_->format(fs, err, sizeof(err))) {
    uiFailure();
    sendError(rid, "format_failed", err[0] ? err : "format failed");
    return;
  }
  refreshStorageUi();
  uiReady();

  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["filesystem"] = fs;
  doc["backend"] = storage_->name();
  doc["mounted"] = storage_->isMounted();
  const StorageInfo si = storage_->info();
  doc["total_bytes"] = si.total_bytes;
  doc["free_bytes"] = si.free_bytes;
  sendJson(doc);
}

void ProtocolServer::cmdHash(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  FileMeta meta{};
  if (!storage_->stat(path, meta) || !meta.exists || meta.is_dir) {
    sendError(rid, "not_found", "file not found");
    return;
  }
  File f = storage_->openRead(path);
  if (!f) {
    sendError(rid, "open_failed", "cannot open file");
    return;
  }
  uint8_t buf[COMSTORAGE_CHUNK_SIZE];
  uint32_t crc = crc32util::init();
  while (true) {
    const size_t n = f.read(buf, sizeof(buf));
    if (n == 0) {
      break;
    }
    crc = crc32util::update(crc, buf, n);
    yield();
  }
  f.close();
  crc = crc32util::finalize(crc);

  JsonDocument doc;
  doc["status"] = "ok";
  doc["request_id"] = rid;
  doc["path"] = path;
  doc["size"] = meta.size;
  doc["crc32"] = crc;
  sendJson(doc);
}

void ProtocolServer::cmdGet(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  FileMeta meta{};
  if (!storage_->stat(path, meta) || !meta.exists || meta.is_dir) {
    sendError(rid, "not_found", "file not found");
    return;
  }
  if (meta.size > COMSTORAGE_MAX_FILE_SIZE) {
    sendError(rid, "too_large", "file exceeds max size");
    return;
  }

  File f = storage_->openRead(path);
  if (!f) {
    sendError(rid, "open_failed", "cannot open file");
    return;
  }

  uiTransfer();

  // Precompute CRC while streaming would require two passes or running CRC during send.
  // We stream once and include running CRC in the final status; client verifies per-chunk CRC.
  JsonDocument ready;
  ready["status"] = "ready";
  ready["request_id"] = rid;
  ready["path"] = path;
  ready["size"] = meta.size;
  ready["chunk_size"] = COMSTORAGE_CHUNK_SIZE;
  sendJson(ready);

  transferActive_ = true;
  uint8_t buf[COMSTORAGE_CHUNK_SIZE];
  uint8_t hdr[4];
  uint8_t crcBuf[4];
  uint32_t totalCrc = crc32util::init();
  uint64_t remaining = meta.size;
  uint32_t errors = 0;

  while (remaining > 0) {
    const size_t toRead = (remaining > COMSTORAGE_CHUNK_SIZE) ? COMSTORAGE_CHUNK_SIZE : (size_t)remaining;
    const size_t n = f.read(buf, toRead);
    if (n != toRead) {
      errors++;
      break;
    }
    const uint32_t chunkCrc = crc32util::compute(buf, n);
    totalCrc = crc32util::update(totalCrc, buf, n);
    writeU32LE(hdr, (uint32_t)n);
    writeU32LE(crcBuf, chunkCrc);
    writeAll(hdr, 4);
    writeAll(buf, n);
    writeAll(crcBuf, 4);
    remaining -= n;
    yield();
  }
  f.close();
  transferActive_ = false;

  // End-of-stream marker: length 0
  writeU32LE(hdr, 0);
  writeAll(hdr, 4);
  Serial.flush();

  JsonDocument done;
  if (errors || remaining != 0) {
    done["status"] = "error";
    done["error"] = "transfer_failed";
    done["message"] = "incomplete read";
    uiFailure();
  } else {
    done["status"] = "ok";
    done["bytes"] = meta.size;
    done["crc32"] = crc32util::finalize(totalCrc);
    uiReady();
  }
  done["request_id"] = rid;
  sendJson(done);
}

void ProtocolServer::cmdPut(JsonDocument &req) {
  const uint32_t rid = requestIdOf(req);
  if (!requireMounted(rid)) {
    return;
  }
  char path[COMSTORAGE_MAX_PATH + 1];
  if (!resolvePath(req, path, sizeof(path), rid)) {
    return;
  }
  if (strcmp(path, "/") == 0) {
    sendError(rid, "forbidden", "cannot write root");
    return;
  }
  if (!req["size"].is<uint32_t>() && !req["size"].is<uint64_t>()) {
    sendError(rid, "bad_request", "missing size");
    return;
  }
  const uint64_t size = req["size"].as<uint64_t>();
  if (size > COMSTORAGE_MAX_FILE_SIZE) {
    sendError(rid, "too_large", "size exceeds max");
    return;
  }

  uint32_t expectedCrc = 0;
  const bool hasCrc = req["crc32"].is<uint32_t>();
  if (hasCrc) {
    expectedCrc = req["crc32"].as<uint32_t>();
  }

  // Ensure parent is root or exists — simple: only write under existing dirs / root files
  File out = storage_->openWrite(path, true);
  if (!out) {
    sendError(rid, "open_failed", "cannot open for write");
    return;
  }

  uiTransfer();

  JsonDocument ready;
  ready["status"] = "ready";
  ready["request_id"] = rid;
  ready["path"] = path;
  ready["size"] = size;
  ready["chunk_size"] = COMSTORAGE_CHUNK_SIZE;
  sendJson(ready);

  transferActive_ = true;
  uint8_t buf[COMSTORAGE_CHUNK_SIZE];
  uint8_t hdr[4];
  uint8_t crcBuf[4];
  uint64_t received = 0;
  uint32_t totalCrc = crc32util::init();
  uint32_t chunkErrors = 0;
  bool failed = false;

  while (received < size) {
    if (!readExact(hdr, 4, COMSTORAGE_CMD_TIMEOUT_MS)) {
      failed = true;
      break;
    }
    const uint32_t chunkLen = readU32LE(hdr);
    if (chunkLen == 0) {
      // unexpected early EOS
      failed = true;
      break;
    }
    if (chunkLen > COMSTORAGE_CHUNK_SIZE || received + chunkLen > size) {
      failed = true;
      chunkErrors++;
      break;
    }
    if (!readExact(buf, chunkLen, COMSTORAGE_CMD_TIMEOUT_MS)) {
      failed = true;
      break;
    }
    if (!readExact(crcBuf, 4, COMSTORAGE_CMD_TIMEOUT_MS)) {
      failed = true;
      break;
    }
    const uint32_t gotCrc = readU32LE(crcBuf);
    const uint32_t calcCrc = crc32util::compute(buf, chunkLen);
    if (gotCrc != calcCrc) {
      chunkErrors++;
      failed = true;
      break;
    }
    if (out.write(buf, chunkLen) != chunkLen) {
      failed = true;
      break;
    }
    totalCrc = crc32util::update(totalCrc, buf, chunkLen);
    received += chunkLen;
    yield();
  }
  out.flush();
  out.close();
  transferActive_ = false;

  const uint32_t finalCrc = crc32util::finalize(totalCrc);

  JsonDocument done;
  done["request_id"] = rid;
  done["path"] = path;
  done["bytes_written"] = received;
  done["crc32"] = finalCrc;
  done["chunk_errors"] = chunkErrors;

  if (failed || received != size) {
    storage_->remove(path);
    done["status"] = "error";
    done["error"] = "transfer_failed";
    done["message"] = "incomplete or corrupt transfer";
    uiFailure();
  } else if (hasCrc && finalCrc != expectedCrc) {
    storage_->remove(path);
    done["status"] = "error";
    done["error"] = "crc_mismatch";
    done["message"] = "file crc32 does not match";
    uiFailure();
  } else {
    done["status"] = "ok";
    refreshStorageUi();
    uiReady();
  }
  sendJson(done);
}
