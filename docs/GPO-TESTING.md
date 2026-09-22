# Pruebas GPO / políticas USB (entorno controlado)

Documento para **medir** el efecto de políticas corporativas sobre USB Mass Storage frente a USB CDC/COM.

**Prohibido en esta PoC:** modificar GPO del dominio, registro para desactivar políticas, Defender/EDR, Device Installation Restrictions bypass, spoofing VID/PID, WDAC/AppLocker bypass, ocultación del dispositivo.

Usa un laboratorio con imagen conocida o un equipo de prueba donde puedas **consultar** (no necesariamente cambiar) las políticas aplicadas.

## Antes de empezar

1. Documenta el host: edición Windows, join a dominio, usuario de prueba.
2. Captura políticas relevantes (solo lectura), por ejemplo:

```powershell
gpresult /h gpresult.html
rsop.msc
```

3. Identifica si existen restricciones del estilo:
   - Removable Storage Access (deny execute/read/write para Removable Disks)
   - Device Installation Restrictions
   - All Removable Storage classes: Deny all access
   - Controles DLP/EDR sobre USB storage vs serie

4. Conecta el dongle y completa [CHECKLIST-STORAGE.md](CHECKLIST-STORAGE.md) y [WINDOWS-ENUMERATION.md](WINDOWS-ENUMERATION.md).

---

## Test A — USB Mass Storage (control positivo)

**Dispositivo:** pendrive USB convencional (clase Mass Storage).

### Pasos

1. Conectar el pendrive.
2. Registrar enumeración:

```powershell
Get-PnpDevice -Class DiskDrive | Format-Table -AutoSize
Get-Disk | Format-Table -AutoSize
Get-Volume | Format-Table -AutoSize
```

3. Intentar lectura/escritura desde el Explorador y desde PowerShell (`Get-ChildItem E:\` etc.).
4. Anotar mensajes de bloqueo (si los hay): acceso denegado, dispositivo deshabilitado, evento DLP, etc.

### Registro

| Pregunta | Resultado |
| --- | --- |
| ¿Se enumera como Disk Drive? | YES / NO |
| ¿Aparece volumen / letra? | YES / NO |
| ¿Se puede listar ficheros? | YES / NO |
| ¿Se puede escribir? | YES / NO |
| ¿Se puede ejecutar desde el medio? | YES / NO / N/A |
| Evidencia (captura / Event Log) | |

**Interpretación:** si el Mass Storage está bloqueado, el Test A debe fallar en acceso (o incluso en enumeración según la política). Eso valida que el control “anti-pendrive” está activo en el host de prueba.

---

## Test B — USB CDC / COM (dispositivo PoC)

**Dispositivo:** T-Dongle-S3 con firmware COMStorage (solo CDC).

### Pasos

1. Conectar el dongle.
2. Verificar que **no** aparece como disco (`Get-Disk` / Disk Management).
3. Verificar que **sí** aparece COMx (`python client.py ports`).
4. Probar canal de datos:

```powershell
python client\client.py info COMx
python client\client.py put COMx .\payload-lab.txt /lab.txt
python client\client.py ls COMx /
python client\client.py get COMx /lab.txt .\out.txt
```

5. Registrar si EDR/DLP genera alertas por actividad en puerto serie (si aplica a tu stack).

### Registro

| Pregunta | Resultado |
| --- | --- |
| ¿Se enumera COM/CDC? | YES / NO |
| ¿Se enumera como Mass Storage? | YES / NO |
| ¿`info`/`put`/`get` funcionan? | YES / NO |
| ¿Política de Removable Storage bloqueó el COM? | YES / NO |
| ¿Device Installation Restrictions bloqueó el COM? | YES / NO |
| Alertas DLP/EDR | |

---

## Test C — Comparativa (resumen ejecutivo)

| Control observado | Mass Storage (A) | CDC/COM PoC (B) |
| --- | --- | --- |
| Enumeración permitida | | |
| Lectura de datos posible | | |
| Escritura de datos posible | | |
| Visible como volumen | | |
| Visible como COMx | | |

### Lecturas típicas

1. **A bloqueado, B permitido:** la política cubre storage USB clásico pero no el canal serie; superficie residual de exfiltración/implantación vía COM.
2. **A y B bloqueados:** control más amplio (USB genérico, allowlist de VID/PID, bloqueo de clase CDC, etc.).
3. **A permitido:** el host de prueba no representa la política corporativa objetivo; repetir en endpoint real de laboratorio.

---

## Evidencia recomendada para informe

- Salida de `Get-PnpDevice`, `Get-Disk`, `Get-Volume`
- `python client.py ports` y `info`
- Capturas de Device Manager (COM vs Disk)
- Fragmento de `gpresult` que cite la política de removable storage (sin secretos)
- Resultado del benchmark (capacidad del canal si está permitido)

## Remediación (orientación defensiva)

Si B está permitido y A no:

- Evaluar restricciones de instalación por clase USB (CDC / Ports) o allowlist de VID/PID corporativos
- Controles DLP en dispositivos serie / USB compuestos
- Monitorización de procesos que abren COMx a alta tasa de transferencia
- Concienciación: “bloquear pendrives” ≠ “bloquear todo USB capaz de transportar datos”

Esta PoC **no** incluye pasos para desactivar esas mitigaciones.
