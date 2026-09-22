# Enumerate COMStorage / USB CDC device evidence on Windows.
# Read-only. Does not change policies or drivers.

param(
    [string]$VidMatch = "303A"
)

Write-Host "=== PnP Ports ===" -ForegroundColor Cyan
Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
    Format-Table Status, FriendlyName, InstanceId -AutoSize

Write-Host "=== PnP DiskDrive ===" -ForegroundColor Cyan
Get-PnpDevice -Class DiskDrive -ErrorAction SilentlyContinue |
    Format-Table Status, FriendlyName, InstanceId -AutoSize

Write-Host "=== Disks ===" -ForegroundColor Cyan
Get-Disk -ErrorAction SilentlyContinue |
    Format-Table Number, FriendlyName, Size, BusType, OperationalStatus -AutoSize

Write-Host "=== Volumes ===" -ForegroundColor Cyan
Get-Volume -ErrorAction SilentlyContinue |
    Format-Table DriveLetter, FileSystemLabel, FileSystem, Size, SizeRemaining -AutoSize

Write-Host "=== USB entities matching VID $VidMatch / Serial / CDC ===" -ForegroundColor Cyan
Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object {
        $_.PNPDeviceID -match $VidMatch -or
        $_.Name -match 'COM\d+|Serial|CDC' -or
        $_.PNPClass -eq 'Ports'
    } |
    Select-Object Name, Status, PNPDeviceID, Manufacturer, Service, PNPClass |
    Format-List

Write-Host "Done. Compare Ports (expected YES) vs DiskDrive/Volume for the dongle (expected NO)." -ForegroundColor Green
