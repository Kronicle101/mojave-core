param([Parameter(Mandatory)][string]$HostBinary, [Parameter(Mandatory)][string]$Proxy, [switch]$Conflict)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:TEMP ('FNVStabilizer-import-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $gameExe = 'FalloutNV.exe'
    $prefix = 'FNVNativeStabilizer'
    $hostPath = Join-Path $testRoot $gameExe
    $proxyPath = Join-Path $testRoot 'd3d9.dll'
    Copy-Item -LiteralPath $HostBinary -Destination $hostPath
    Copy-Item -LiteralPath $Proxy -Destination $proxyPath
    if ($Conflict) { & $hostPath $proxyPath conflict } else { & $hostPath $proxyPath }
    if ($LASTEXITCODE -ne 0) { throw "Import host failed: $LASTEXITCODE" }
    Get-Content -LiteralPath (Join-Path $testRoot ($prefix + '.log'))
} finally {
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolvedTest.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTest -Recurse -Force
    }
}
