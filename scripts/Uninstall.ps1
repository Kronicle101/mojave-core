[CmdletBinding()]
param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas')
$ErrorActionPreference = 'Stop'
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
if (Get-Process -Name FalloutNV,FalloutNVLauncher -ErrorAction SilentlyContinue) { throw 'Cierra Fallout: New Vegas y su launcher antes de desinstalar.' }
$manifestPath = Join-Path $gameRoot 'FNVNativeStabilizer.install.json'
if (!(Test-Path -LiteralPath $manifestPath)) { throw 'No existe una instalacion registrada.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.gamePath -ne $gameRoot) { throw 'El manifiesto pertenece a otra carpeta.' }
$proxy = Join-Path $gameRoot 'd3d9.dll'
$entry = $manifest.files | Where-Object { $_.name -eq 'd3d9.dll' }
if (!$entry) { throw 'Manifiesto sin hash del proxy.' }
if ((Test-Path -LiteralPath $proxy) -and (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -ne $entry.sha256) {
    throw 'd3d9.dll cambio desde la instalacion. Se conserva para evitar borrar otro mod.'
}
if (Test-Path -LiteralPath $proxy) { Remove-Item -LiteralPath $proxy }
$ini = Join-Path $gameRoot 'FNVNativeStabilizer.ini'
if (Test-Path -LiteralPath $ini) {
    $backup = Join-Path $gameRoot ('FNVNativeStabilizer.config-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff') + '.bak')
    Move-Item -LiteralPath $ini -Destination $backup
    Write-Output "Configuracion conservada en $backup"
}
if ($manifest.windowSetting -and (Test-Path -LiteralPath $manifest.windowSetting.path)) {
    $prefs = $manifest.windowSetting.path
    $text = [IO.File]::ReadAllText($prefs)
    $match = [regex]::Match($text, '(?im)^bFull Screen\s*=\s*(\d+)\s*$')
    if ($match.Success -and $match.Groups[1].Value -eq $manifest.windowSetting.installed) {
        $replacement = '${1}' + $manifest.windowSetting.original + '${2}'
        $restored = [regex]::Replace($text, '(?im)^(bFull Screen\s*=\s*)\d+(\s*)$', $replacement)
        [IO.File]::WriteAllText($prefs, $restored, [Text.UTF8Encoding]::new($false))
    }
}
Remove-Item -LiteralPath $manifestPath
Write-Output 'Desinstalado. El ejecutable, guardados y registro de diagnostico se conservan.'
