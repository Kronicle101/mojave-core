[CmdletBinding()]
param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas',
      [string]$PrefsPath = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\FalloutNV\FalloutPrefs.ini'))
$ErrorActionPreference = 'Stop'
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
if (Get-Process FalloutNV,FalloutNVLauncher -ErrorAction SilentlyContinue) { throw 'Cierra el juego y el launcher para actualizar.' }
$manifestPath = Join-Path $gameRoot 'FNVNativeStabilizer.install.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.gamePath -ne $gameRoot) { throw 'El manifiesto pertenece a otra carpeta.' }
$entry = $manifest.files | Where-Object { $_.name -eq 'd3d9.dll' }
$proxy = Join-Path $gameRoot 'd3d9.dll'
if (!$entry -or (Get-FileHash -LiteralPath $proxy).Hash -ne $entry.sha256) { throw 'La DLL instalada no coincide con la instalacion registrada.' }
$packageRoot = Split-Path -Parent $PSScriptRoot
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff')
$backupRoot = Join-Path $gameRoot ('FNVNativeStabilizer-backup-' + $stamp)
New-Item -ItemType Directory -Path $backupRoot | Out-Null
$names = @('d3d9.dll', 'FNVNativeStabilizer.ini', 'FNVNativeStabilizer.install.json')
foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $gameRoot $name) -Destination $backupRoot }
try {
    Copy-Item -LiteralPath (Join-Path $packageRoot 'd3d9.dll') -Destination $proxy -Force
    # Preserve previous config in the backup; install the explicitly requested native mode.
    Copy-Item -LiteralPath (Join-Path $packageRoot 'FNVNativeStabilizer.ini') -Destination (Join-Path $gameRoot 'FNVNativeStabilizer.ini') -Force
    # This audio update does not change launcher preferences or presentation.
    $manifest.version = '0.4.4'
    $manifest.files = @('d3d9.dll', 'FNVNativeStabilizer.ini') | ForEach-Object {
        @{ name = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $gameRoot $_)).Hash }
    }
    $manifest | Add-Member -NotePropertyName previousVersionBackup -NotePropertyValue $backupRoot -Force
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Output "Actualizado a 0.4.4. Copia anterior: $backupRoot. Paso fisico nativo con acumulador independiente del render; preferencias conservadas."
} catch {
    foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $backupRoot $name) -Destination (Join-Path $gameRoot $name) -Force }
    throw
}
