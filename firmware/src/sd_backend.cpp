#include "sd_backend.h"
#include "board_config.h"
#include <SD_MMC.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

extern "C" {
#include "ff.h"
}

// FatFs volume→partition map (FF_MULTI_PARTITION=1 in ESP-IDF FatFs).
extern "C" PARTITION VolToPart[];

bool SDBackend::mountInternal(bool oneBitMode) {
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  return SD_MMC.begin("/sdcard", oneBitMode);
}

bool SDBackend::begin() {
  mounted_ = mountInternal(false);
  if (!mounted_) {
    mounted_ = mountInternal(true);
  }
  return mounted_;
}

void SDBackend::end() {
  if (mounted_) {
    SD_MMC.end();
    mounted_ = false;
  }
}

StorageInfo SDBackend::info() const {
  StorageInfo i{};
  i.backend = name();
  i.mounted = mounted_;
  if (!mounted_) {
    return i;
  }
  i.total_bytes = SD_MMC.totalBytes();
  i.used_bytes = SD_MMC.usedBytes();
  i.free_bytes = (i.total_bytes > i.used_bytes) ? (i.total_bytes - i.used_bytes) : 0;
  return i;
}

bool SDBackend::exists(const char *path) { return SD_MMC.exists(path); }

bool SDBackend::isDirectory(const char *path) {
  File f = SD_MMC.open(path);
  if (!f) {
    return false;
  }
  const bool d = f.isDirectory();
  f.close();
  return d;
}

bool SDBackend::mkdir(const char *path) { return SD_MMC.mkdir(path); }

bool SDBackend::remove(const char *path) {
  if (isDirectory(path)) {
    return SD_MMC.rmdir(path);
  }
  return SD_MMC.remove(path);
}

bool SDBackend::rename(const char *from, const char *to) { return SD_MMC.rename(from, to); }

bool SDBackend::stat(const char *path, FileMeta &meta) {
  meta = {};
  if (!exists(path)) {
    meta.exists = false;
    return true;
  }
  File f = SD_MMC.open(path);
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

bool SDBackend::list(const char *path,
                     bool (*listCb)(const char *name, bool isDir, uint64_t size, void *ctx),
                     void *ctx) {
  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }
  File entry = dir.openNextFile();
  while (entry) {
    const char *name = entry.name();
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

File SDBackend::openRead(const char *path) { return SD_MMC.open(path, FILE_READ); }

File SDBackend::openWrite(const char *path, bool truncate) {
  return SD_MMC.open(path, truncate ? FILE_WRITE : FILE_APPEND);
}

bool SDBackend::format(const char *fsType, char *errMsg, size_t errLen) {
  auto setErr = [&](const char *msg) {
    if (errMsg && errLen) {
      snprintf(errMsg, errLen, "%s", msg);
    }
  };

  if (!fsType) {
    setErr("missing filesystem type");
    return false;
  }

  char lower[16];
  size_t n = 0;
  for (; fsType[n] && n + 1 < sizeof(lower); ++n) {
    lower[n] = (char)tolower((unsigned char)fsType[n]);
  }
  lower[n] = '\0';

  if (strcmp(lower, "ntfs") == 0) {
    setErr("NTFS is not supported by ESP32. Format FAT32 on a PC for dual use, "
           "or use device format fat32 (MBR+FAT32).");
    return false;
  }

  if (strcmp(lower, "fat32") != 0 && strcmp(lower, "fat") != 0) {
    setErr("unsupported filesystem (only fat32)");
    return false;
  }

  // Disk I/O is registered only while mounted — keep mounted for f_fdisk/f_mkfs.
  if (!mounted_) {
    if (!begin()) {
      setErr("SD card not mounted — insert a card and retry");
      return false;
    }
  }

  const size_t workSize = 4096;
  void *work = malloc(workSize);
  if (!work) {
    setErr("out of memory for format work buffer");
    return false;
  }

  // Physical drive for the SDMMC FatFs binding is typically 0 when only SD is used.
  const BYTE pdrv = 0;

  // Bind logical volume 0 to partition #1 (MBR primary) — required for Windows SD readers.
  // pt=0 means "auto/SFD"; that is what broke PC detection previously.
  VolToPart[0].pd = pdrv;
  VolToPart[0].pt = 1;

  // Create MBR with a single primary partition using 100% of the card.
  DWORD plist[4] = {100, 0, 0, 0};
  FRESULT frDisk = f_fdisk(pdrv, plist, work);
  if (frDisk != FR_OK && frDisk != FR_EXIST) {
    // Continue: some cards already have a usable MBR; mkfs may still succeed.
    Serial.printf("{\"event\":\"log\",\"msg\":\"f_fdisk status\",\"code\":%d}\n", (int)frDisk);
  }

  // Format the first partition as FAT32. au=0 → FatFs picks a Windows-like cluster size.
  // Do NOT set FM_SFD (super-floppy / no partition table) — PCs often reject that on SDHC.
  FRESULT fr = f_mkfs("0:", FM_FAT32, 0, work, workSize);
  if (fr != FR_OK) {
    fr = f_mkfs("0:", FM_ANY, 0, work, workSize);
  }
  free(work);

  if (fr != FR_OK) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "f_mkfs failed (FRESULT=%d, f_fdisk=%d). Recover card with Windows FAT32 "
             "format, then retry.",
             (int)fr, (int)frDisk);
    setErr(buf);
    VolToPart[0].pt = 0;  // restore auto-detect for remount attempts
    end();
    begin();
    return false;
  }

  // Remount so VFS/FatFs rescans the new MBR+FAT32 layout.
  end();
  // Keep binding to partition 1 for subsequent mounts in this session.
  VolToPart[0].pd = pdrv;
  VolToPart[0].pt = 1;

  if (!begin()) {
    // Fall back to auto-detect partition (pt=0) which finds first FAT volume.
    VolToPart[0].pt = 0;
    if (!begin()) {
      setErr("format wrote MBR+FAT32 but remount failed — try reinserting the card");
      return false;
    }
  }

  // Sanity check: create and read a tiny marker so we know the volume is usable.
  File probe = SD_MMC.open("/.comstorage", FILE_WRITE);
  if (probe) {
    probe.print("ok");
    probe.close();
    SD_MMC.remove("/.comstorage");
  }

  return true;
}
