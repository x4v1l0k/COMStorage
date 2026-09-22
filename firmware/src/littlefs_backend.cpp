#include "littlefs_backend.h"
#include <LittleFS.h>
#include <string.h>

bool LittleFSBackend::begin() {
  // Partition label "spiffs" in partitions.csv is the usual Arduino-ESP32 label
  // even when the filesystem type is LittleFS.
  mounted_ = LittleFS.begin(true);
  return mounted_;
}

void LittleFSBackend::end() {
  if (mounted_) {
    LittleFS.end();
    mounted_ = false;
  }
}

StorageInfo LittleFSBackend::info() const {
  StorageInfo i{};
  i.backend = name();
  i.mounted = mounted_;
  if (!mounted_) {
    return i;
  }
  i.total_bytes = LittleFS.totalBytes();
  i.used_bytes = LittleFS.usedBytes();
  i.free_bytes = (i.total_bytes > i.used_bytes) ? (i.total_bytes - i.used_bytes) : 0;
  return i;
}

bool LittleFSBackend::exists(const char *path) { return LittleFS.exists(path); }

bool LittleFSBackend::isDirectory(const char *path) {
  File f = LittleFS.open(path);
  if (!f) {
    return false;
  }
  const bool d = f.isDirectory();
  f.close();
  return d;
}

bool LittleFSBackend::mkdir(const char *path) { return LittleFS.mkdir(path); }

bool LittleFSBackend::remove(const char *path) {
  if (isDirectory(path)) {
    return LittleFS.rmdir(path);
  }
  return LittleFS.remove(path);
}

bool LittleFSBackend::rename(const char *from, const char *to) { return LittleFS.rename(from, to); }

bool LittleFSBackend::stat(const char *path, FileMeta &meta) {
  meta = {};
  if (!exists(path)) {
    meta.exists = false;
    return true;
  }
  File f = LittleFS.open(path);
  if (!f) {
    return false;
  }
  meta.exists = true;
  meta.is_dir = f.isDirectory();
  meta.size = meta.is_dir ? 0 : f.size();
  meta.mtime = 0;
  f.close();
  return true;
}

bool LittleFSBackend::list(const char *path,
                           bool (*listCb)(const char *name, bool isDir, uint64_t size, void *ctx),
                           void *ctx) {
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }
  File entry = dir.openNextFile();
  while (entry) {
    const char *name = entry.name();
    // LittleFS may return full path; strip to basename for protocol.
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    const bool isDir = entry.isDirectory();
    const uint64_t size = isDir ? 0 : entry.size();
    entry.close();
    if (!listCb(base, isDir, size, ctx)) {
      dir.close();
      return true;
    }
    entry = dir.openNextFile();
  }
  dir.close();
  return true;
}

File LittleFSBackend::openRead(const char *path) { return LittleFS.open(path, FILE_READ); }

File LittleFSBackend::openWrite(const char *path, bool truncate) {
  return LittleFS.open(path, truncate ? FILE_WRITE : FILE_APPEND);
}
