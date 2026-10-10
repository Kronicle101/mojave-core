[CmdletBinding()]
param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas',
      [string]$PrefsPath = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\FalloutNV\FalloutPrefs.ini'))
$ErrorActionPreference = 'Stop'
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
$exe = Join-Path $gameRoot 'FalloutNV.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'No se encontro FalloutNV.exe.' }
if (Get-Process -Name FalloutNV,FalloutNVLauncher -ErrorAction SilentlyContinue) { throw 'Cierra Fallout: New Vegas y su launcher antes de instalar.' }
$version = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
if ($version -ne '1.4.0.525') { throw "Version no validada: $version. Se admite 1.4.0.525." }
$packageRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $gameRoot 'FNVNativeStabilizer.install.json'
if (Test-Path -LiteralPath $manifestPath) { throw 'Ya existe una instalacion registrada. Desinstala antes de reinstalar.' }
$files = @('d3d9.dll', 'FNVNativeStabilizer.ini')
foreach ($name in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $packageRoot $name) -PathType Leaf)) { throw "Falta en el paquete: $name" }
    if (Test-Path -LiteralPath (Join-Path $gameRoot $name)) { throw "Ya existe $name. No se reemplazara un archivo o wrapper ajeno." }
}
$entries = @()
try {
    foreach ($name in $files) {
        $source = Join-Path $packageRoot $name
        $destination = Join-Path $gameRoot $name
        Copy-Item -LiteralPath $source -Destination $destination
        $entries += @{ name = $name; sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash }
    }
    $manifest = @{
        version = '0.4.4'; gameVersion = $version; gamePath = $gameRoot
        exeSHA256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        executableModified = $false; files = $entries
        installedAtUTC = [DateTime]::UtcNow.ToString('o')
    }
    $borderless = [IO.File]::ReadAllText((Join-Path $gameRoot 'FNVNativeStabilizer.ini')) -match '(?im)^Borderless\s*=\s*1\s*$'
    $desiredFullscreen = if ($borderless) {'0'} else {'1'}
    $manifest.windowSetting = & (Join-Path $PSScriptRoot 'ConfigureWindow.ps1') -PrefsPath $PrefsPath -FullscreenValue $desiredFullscreen
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Output "Instalado en $gameRoot. Inicia desde Steam normalmente. Sin NVSE ni gestor de mods."
} catch {
    foreach ($entry in $entries) {
        $destination = Join-Path $gameRoot $entry.name
        if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination).Hash -eq $entry.sha256) {
            Remove-Item -LiteralPath $destination
        }
    }
    throw
}
