# Protocolo COMStorage v1.0

Transporte: **USB CDC** (puerto COM). Framing:

1. **Control:** una línea JSON por mensaje, terminada en `\n` (UTF-8).
2. **Datos:** tras un `status=ready` de `get`/`put`, chunks binarios.

No hay shell. Solo comandos de la allowlist.

## Comandos

| cmd | Descripción |
| --- | --- |
| `ping` | Latencia / liveness |
| `info` | Dispositivo + límites |
| `storage` | Capacidad del backend |
| `list` / `ls` | Listado de directorio |
| `stat` | Metadatos |
| `get` | Descarga (binario) |
| `put` | Subida (binario) |
| `delete` / `rm` | Borrado |
| `mkdir` | Crear directorio |
| `rename` / `move` / `mv` | Renombrar o mover (`path` + `new_path` o `to`) |
| `touch` | Crear fichero vacío |
| `chmod` | Siempre `not_supported` (FAT sin permisos POSIX) |
| `format` | Formatea la SD (`filesystem`=`fat32`\|`ntfs`, `confirm`=`yes`) |
| `hash` | CRC32 del fichero |

Todas las peticiones deberían incluir `request_id` (entero).

### info

```json
{"cmd":"info","request_id":1}
```

```json
{
  "status":"ok",
  "request_id":1,
  "device":"TTGO-T-Dongle-S3",
  "protocol_version":"1.0",
  "transport":"USB-CDC",
  "storage":"LittleFS"
}
```

### list

```json
{"cmd":"list","request_id":2,"path":"/"}
```

### stat / delete / mkdir / hash

```json
{"cmd":"stat","request_id":3,"path":"/test.txt"}
{"cmd":"delete","request_id":4,"path":"/test.txt"}
{"cmd":"mkdir","request_id":5,"path":"/logs"}
{"cmd":"hash","request_id":6,"path":"/test.txt"}
```

### format (destructivo)

```json
{"cmd":"format","request_id":20,"filesystem":"fat32","confirm":"yes"}
```

- `confirm` debe ser exactamente `"yes"`.
- `filesystem`: `fat32` (soportado). Crea **MBR + partición FAT32** (compatible PC + dongle).
- `ntfs` se rechaza: el ESP32 no puede montarlo.
- **No uses** el formateo antiguo tipo “super-floppy” (sin MBR): Windows suele no detectar la tarjeta.
- Si la SD ya quedó ilegible en el PC: recupérala formateando en Windows (FAT32) o con [SD Memory Card Formatter](https://www.sdcard.org/downloads/formatter/), y después úsala normalmente (recomendado) o vuelve a formatear desde el dongle con este firmware corregido.


### put

1. Cliente → `{"cmd":"put","request_id":10,"path":"/f.bin","size":1234,"crc32":...}`
2. Dispositivo → `{"status":"ready","request_id":10,"size":1234,"chunk_size":1024,"chunk_ack":true}`
3. Cliente envía **un** chunk y espera `{"status":"chunk_ok","request_id":10,"received":N}`
4. Repetir hasta completar `size` bytes
5. Dispositivo → `{"status":"ok","request_id":10,"bytes_written":1234,"crc32":...}`

> `chunk_ack` evita overrun del buffer RX del USB CDC en el ESP32-S3 (síntoma típico: `bytes_written=0`, `chunk_errors=1`).

### get

1. Cliente → `{"cmd":"get","request_id":11,"path":"/f.bin"}`
2. Dispositivo → `{"status":"ready",...}`
3. Dispositivo envía chunks
4. Dispositivo envía marcador EOS (`length=0`)
5. Dispositivo → `{"status":"ok","request_id":11,"bytes":...,"crc32":...}`

### Formato de chunk

Little-endian:

```
uint32 length
uint8  data[length]
uint32 crc32   // CRC32 IEEE del data del chunk
```

`length == 0` (solo en get, sin CRC) = fin de stream.

## Seguridad

- Paths: solo `/` + componentes `[A-Za-z0-9._\- ]`
- Rechazo: `..`, `\`, `C:\...`, caracteres especiales
- Jail al filesystem montado (raíz virtual `/`)
- `COMSTORAGE_MAX_FILE_SIZE` (default 12 MiB)
- `COMSTORAGE_CHUNK_SIZE` 1024
- `COMSTORAGE_RX_LINE_MAX` 512
- Timeouts de lectura binaria
- CRC por chunk; ACK por chunk en `put` (`chunk_ok`); CRC de fichero opcional en `put`
- Sin comandos de ejecución / shell / Wi-Fi / flash raw

## Eventos asíncronos

El firmware puede emitir líneas `{"event":"..."}` (boot, log). El cliente las ignora salvo logging.
