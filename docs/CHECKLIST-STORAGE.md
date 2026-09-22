# Checklist — ¿Windows lo trata como almacenamiento?

Ejecutar **sin modificar políticas**. Solo observación.

## Preparación

- [ ] Firmware COMStorage flasheado
- [ ] Dongle conectado
- [ ] `python client.py ports` muestra un COM del dispositivo
- [ ] `python client.py info COMx` responde JSON ok

## Comprobaciones COM (esperado: SÍ)

- [ ] Device Manager → Puertos (COM y LPT) → COMx presente
- [ ] `Get-PnpDevice -Class Ports` lista el dispositivo
- [ ] Transferencia `put`/`get` funciona por el COM

## Comprobaciones disco (esperado: NO)

- [ ] Device Manager → Unidades de disco → **ausente**
- [ ] `Get-Disk` → **no** aparece disco nuevo del dongle
- [ ] `Get-Volume` → **no** hay volumen/letra nueva
- [ ] `Get-PnpDevice -Class DiskDrive` → sin instancia del dongle
- [ ] Explorador de archivos → **sin** unidad extraíble nueva

## Resultado PoC (rellenar)

```
Fecha:
Host / build Windows:
Política USB Mass Storage (si conocida):

COM device:          YES / NO
Disk device:         YES / NO
Volume:              YES / NO
USB Mass Storage:    YES / NO

VID/PID:
Friendly name:
Notas:
```

## Interpretación

| COM | Disk/Volume | Lectura |
| --- | --- | --- |
| YES | NO | Canal serie permitido; no clasificado como storage (resultado buscado de la PoC) |
| NO | NO | Posible bloqueo amplio de USB / CDC / instalación de dispositivos |
| YES | YES | Firmware incorrecto o interfaz MSC habilitada por error — revisar build |
| NO | YES | Anómalo; revisar qué dispositivo se conectó |
