# Enumeración del dispositivo en Windows

Objetivo: registrar **cómo Windows ve el dongle** cuando corre el firmware COMStorage (CDC). No se oculta ni se altera la identidad USB.

## Device Manager

1. Conecta el T-Dongle-S3 con el firmware PoC.
2. Abre **Administrador de dispositivos**.
3. Revisa:
   - **Puertos (COM y LPT)** → debería aparecer un COMx (p. ej. "USB Serial Device (COMx)" o similar Espressif).
   - **Unidades de disco** → **no** debería aparecer el dongle.
   - **Dispositivos de almacenamiento** / USB Mass Storage → **no**.
4. Propiedades del COM → Detalles:
   - Id. de instancia del dispositivo
   - Id. de hardware (`USB\VID_xxxx&PID_yyyy...`)
   - Guid de clase
   - Proveedor del controlador

## PowerShell — inventario rápido

```powershell
Get-PnpDevice | Where-Object {
  $_.FriendlyName -match 'COM|Serial|CDC|UART|USB' -or $_.InstanceId -match 'USB\\'
} | Format-Table Status, Class, FriendlyName, InstanceId -AutoSize
```

Filtrar puertos:

```powershell
Get-PnpDevice -Class Ports | Format-List *
```

## WMI / CIM

```powershell
Get-CimInstance Win32_PnPEntity |
  Where-Object { $_.Name -match 'COM\d+' -or $_.PNPClass -eq 'Ports' } |
  Select-Object Name, Status, PNPDeviceID, Manufacturer, Service |
  Format-List
```

Discos y volúmenes (para contrastar):

```powershell
Get-Disk
Get-Volume
Get-PnpDevice -Class DiskDrive
Get-PnpDevice -Class USB
```

## pnputil

```powershell
pnputil /enum-devices /connected
pnputil /enum-devices /class Ports
pnputil /enum-devices /instanceid "USB\VID_303A&PID_...."
```

(Sustituye el instance id real obtenido de Device Manager / Get-PnpDevice.)

## Script del repositorio

```powershell
.\scripts\Enumerate-ComStorageDevice.ps1
```

## Datos a capturar (evidencia)

| Campo | Cómo obtenerlo | Valor observado |
| --- | --- | --- |
| VID | Hardware ID / pyserial | |
| PID | Hardware ID / pyserial | |
| USB class | Descriptor / Inf | CDC (02h) esperado |
| Subclass / Protocol | Descriptor | |
| Device instance ID | PnP | |
| Driver / Service | Win32_PnPEntity.Service | tip. `usbser` / `UsbSer` |
| Manufacturer | PnP | |
| Friendly name | PnP | |
| COMx | Device Manager / `ports` | |

## Cliente Python

```powershell
python client\client.py ports
```

Muestra COM, VID, PID, manufacturer, product, serial, description.
