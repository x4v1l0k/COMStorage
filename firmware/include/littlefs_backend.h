#pragma once

#include "storage_backend.h"

class LittleFSBackend : public StorageBackend {
 public:
  bool begin() override;
  void end() override;
  const char *name() const override { return "LittleFS"; }
  bool isMounted() const override { return mounted_; }
  StorageInfo info() const override;

  bool exists(const char *path) override;
  bool isDirectory(const char *path) override;
  bool mkdir(const char *path) override;
  bool remove(const char *path) override;
  bool rename(const char *from, const char *to) override;
  bool stat(const char *path, FileMeta &meta) override;
  bool list(const char *path,
            bool (*listCb)(const char *name, bool isDir, uint64_t size, void *ctx),
            void *ctx) override;
  File openRead(const char *path) override;
  File openWrite(const char *path, bool truncate) override;

 private:
  bool mounted_ = false;
};
