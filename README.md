# COMStorage

**Proof of Concept** — filesystem over **USB CDC / Serial (COM)** on a LILYGO TTGO T-Dongle-S3 (ESP32-S3).

Lab tool to measure whether a Windows GPO that blocks **USB Mass Storage** still allows file transfer when the device enumerates **only** as a serial port and talks a custom JSON + binary protocol.

> **Scope:** authorized lab / Red Team control validation.  
> **Out of scope:** GPO/registry/EDR bypass, VID/PID spoofing, USB MSC, driver hiding.

---

## Why this exists

| Control surface | Typical USB stick | This PoC |
| --- | --- | --- |
| USB Mass Storage (MSC) | Yes | **No** |
| Disk Drive / Volume letter | Yes | **No** |
| COM / CDC serial port | Rare | **Yes** |
| File I/O path | Windows filesystem | Custom protocol → microSD |

The dongle appears as an Espressif native USB CDC device (VID `303A`). Storage lives on an inserted **microSD (FAT32)** accessed only through the protocol — never as a Windows drive.

---

## Architecture

```
┌─────────────┐     USB CDC (COMx)      ┌──────────────────────┐
│   Windows   │ ◄────────────────────► │  ESP32-S3 T-Dongle   │
│  CLI / GUI  │   JSON + binary chunks │  COMStorage firmware │
└─────────────┘        + CRC32         └──────────┬───────────┘
                                                   │ SDMMC
                                                   ▼
                                            ┌────────────┐
                                            │  microSD   │
                                            │   FAT32    │
                                            └────────────┘
```

- **Transport:** USB CDC only (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`)
- **Storage:** SD card via SDMMC — **not** LittleFS flash, **not** MSC
- **UI on device:** ST7735 160×80 status (Ready / Failure / Transferring + capacity)
- **Host:** Python CLI + FileZilla-style dual-pane GUI

---

## Repository layout

```
firmware/     PlatformIO firmware (ESP32-S3, USB CDC, SD, display)
client/       Python library, CLI, and tkinter GUI
docs/         Hardware, protocol, Windows enumeration, GPO tests
scripts/      Read-only PowerShell PnP inventory helper
```

---

## Requirements

| Side | Need |
| --- | --- |
| Hardware | [LILYGO TTGO T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3), microSD (FAT/FAT32), USB Type-A host |
| Firmware | [PlatformIO](https://platformio.org/) + ESP32 platform |
| Host | Windows 10/11, Python 3.10+, `pyserial` |

---

## Quick start

### 1. Flash firmware

```bash
cd firmware
pio run -t upload
pio device monitor
```

If upload fails on the CDC port: hold **BOOT**, re-plug (or reset), release **BOOT**, retry.

Insert a microSD before use. Without a card the display shows `Failure` / `NO-SD`.

### 2. Install client

```powershell
cd client
python -m pip install -r requirements.txt
```

### 3. List ports (dongle filter)

```powershell
python client.py ports          # Espressif / VID 303A only
python client.py ports --all    # every COM port
```

### 4. CLI examples

Replace `COM7` with the port from `ports`:

```powershell
python client.py info COM7
python client.py ls COM7 /
python client.py put COM7 .\test.txt /test.txt
python client.py get COM7 /test.txt .\out.txt
python client.py rename COM7 /test.txt /renamed.txt
python client.py touch COM7 /empty.txt
python client.py delete COM7 /renamed.txt
python client.py format COM7 --filesystem fat32 --confirm yes
python client.py benchmark COM7
```

`format` is destructive. It requires `--confirm yes` exactly. Supported filesystem on-device: **FAT32** (NTFS is rejected by FatFs on ESP32). After formatting, the card remains readable on a PC as a normal FAT32 volume when mounted in a card reader.

### 5. GUI

```powershell
python gui.py
```

- Left pane: local filesystem  
- Right pane: remote SD over COM  
- Port combo filters to likely Espressif CDC devices (tick **Show all COM** to override)  
- Dark theme, dual explorer, transfer / rename / mkdir / format (with confirmation)

---

## Expected Windows enumeration

| Interface | Expected |
| --- | --- |
| Ports (COM / CDC) | **Present** |
| Disk Drive / Removable Disk for the dongle | **Absent** |
| Volume / drive letter for the dongle | **Absent** |
| USB Mass Storage class for the dongle | **Absent** |

Read-only inventory:

```powershell
.\scripts\Enumerate-ComStorageDevice.ps1
```

More detail:

- [docs/CHECKLIST-STORAGE.md](docs/CHECKLIST-STORAGE.md) — pass/fail checklist  
- [docs/WINDOWS-ENUMERATION.md](docs/WINDOWS-ENUMERATION.md) — PnP evidence  
- [docs/GPO-TESTING.md](docs/GPO-TESTING.md) — Mass Storage vs CDC test plan  
- [docs/PROTOCOL.md](docs/PROTOCOL.md) — wire protocol  
- [docs/HARDWARE.md](docs/HARDWARE.md) — board pins and backend notes  

---

## Protocol (summary)

1. **Control plane:** one JSON object per line (`\n`-terminated UTF-8).  
2. **Data plane:** after `get`/`put` ready, little-endian chunks: `u32 length` + payload + `u32 crc32`.
   On `put`, the device ACKs each chunk (`chunk_ok`) so USB CDC RX does not overrun.

Allowlisted commands only — no remote shell, no code execution:

`ping`, `info`, `storage`, `list`/`ls`, `stat`, `get`, `put`, `delete`/`rm`, `mkdir`, `rename`/`move`/`mv`, `touch`, `hash`, `format`, `chmod` → `not_supported`.

Hardening for a PoC:

- Path sanitization (reject `..`, `\`, drive letters)
- Size / line / chunk limits
- Per-chunk CRC32 (+ optional whole-file CRC on `put`)
- Command timeouts and RX buffer reset on disconnect

Full schema: [docs/PROTOCOL.md](docs/PROTOCOL.md)

---

## Hardware snapshot

| Item | Value |
| --- | --- |
| MCU | ESP32-S3 |
| Flash | 16 MB (firmware + unused data partition; **not** used as FS in this build) |
| USB | Native S3 CDC on **Type-A** dongle plug |
| Display | ST7735 0.96" 160×80 (landscape, backlight active-LOW) |
| LED | APA102 |
| Storage | microSD via **SDMMC**, FAT32 |

---

## Security & authorization

This project is for **authorized** security assessments and controlled labs (e.g. measuring Removable Storage / Device Installation / DLP gaps around CDC).

It does **not**:

- Alter Group Policy, registry, Defender, EDR, WDAC, or AppLocker  
- Spoof USB identity or hide the device  
- Implement USB Mass Storage  

Use only on systems and engagements where you are permitted to connect test hardware and transfer files.

---

## License / use

Internal / engagement lab use unless otherwise stated by the repository owner. Do not use to circumvent security policy outside an authorized scope.
