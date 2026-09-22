# Hardware — LILYGO TTGO T-Dongle-S3

Fuentes: wiki oficial LilyGO, repositorio [Xinyuan-LilyGO/T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3).

## Microcontrolador

- **SoC:** ESP32-S3
- **Flash:** 16 MB Quad-SPI (QIO 80 MHz)
- **SRAM:** 512 KB
- **PSRAM:** no disponible (debe permanecer deshabilitado)
- **Radio:** Wi-Fi 802.11 b/g/n + Bluetooth 5 LE (no usados en esta PoC)

## Revisiones

Existen variantes (T-Dongle-S3, Dual, Plus). Esta PoC apunta a **T-Dongle-S3** clásico (pantalla + TF + APA102). Verifica silk/PCB si tu unidad es Plus/Dual.

## USB

- Conector: **USB Type-A macho** (formato dongle)
- Controlador USB: **PHY USB nativo del ESP32-S3** (no chip UART-USB externo)
- Modo firmware PoC: **Hardware CDC / JTAG** (`ARDUINO_USB_MODE=1`)
- CDC on boot: habilitado (`ARDUINO_USB_CDC_ON_BOOT=1`)
- **USB Mass Storage:** no implementado / no habilitado

En Windows el dispositivo debería aparecer como puerto serie (COM), típico VID Espressif `303A`.

## GPIO relevantes

| Función | GPIO |
| --- | --- |
| APA102 DIN | 40 |
| APA102 CLK | 39 |
| Botón BOOT | 0 |
| TFT CS | 4 |
| TFT MOSI | 3 |
| TFT SCLK | 5 |
| TFT DC | 2 |
| TFT RST | 1 |
| TFT backlight | 38 |
| SDMMC D0 | 14 |
| SDMMC D1 | 17 |
| SDMMC D2 | 21 |
| SDMMC D3 | 18 |
| SDMMC CLK | 12 |
| SDMMC CMD | 16 |
| QWIIC TX | 43 |
| QWIIC RX | 44 |

## Almacenamiento

### Flash interna + LittleFS (backend por defecto)

- Partición `spiffs` en `partitions.csv` (~12.8 MB) montada con **LittleFS**
- Siempre disponible sin tarjeta TF
- Limitaciones: capacidad, endurance de NOR flash, sin metadatos de tiempo reales

### Almacenamiento externo

- La placa **sí** incluye ranura TF (microSD) vía SDMMC
- **Backend por defecto / obligatorio:** tarjeta SD insertada (`SD-FAT`)
- LittleFS en flash **ya no se usa** como almacenamiento de la PoC
- Formateo desde el protocolo/GUI: **FAT32** (NTFS no soportado por ESP32 FatFs)
- Si no hay SD o no monta: pantalla `Failure` / `NO-SD`

### Abstracción

```
StorageBackend
 ├── LittleFSBackend   (default)
 └── SDBackend         (opcional)
```

Protocolo y cliente Windows son independientes del backend.

## Pantalla / LED / botón

- Pantalla ST7735: landscape 160×80; muestra COMStorage, estado y estadísticas de almacenamiento
- LED APA102: azul=boot, verde=listo, amarillo=pulso de botón
- Botón BOOT: refresca estadísticas en pantalla; no abre shell

## Filesystems posibles

| FS | ¿Viable? | Notas |
| --- | --- | --- |
| LittleFS | Sí | Default PoC |
| FAT/FATFS en flash | Posible | No usado |
| FAT en TF | Sí vía SD_MMC | Backend opcional |
| SPIFFS | Legacy | Evitar; LittleFS preferido |
