param([Parameter(Mandatory)][string]$HostBinary, [Parameter(Mandatory)][string]$Proxy, [int]$DX12 = 1, [switch]$NativeFocusHost)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:TEMP ('FNVStabilizer-graphics-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $gameExe = 'FalloutNV.exe'
    $prefix = 'FNVNativeStabilizer'
    $hostPath = Join-Path $testRoot $(if ($NativeFocusHost) {$gameExe} else {'graphics_smoke.exe'})
    $proxyPath = Join-Path $testRoot 'd3d9.dll'
    Copy-Item -LiteralPath $HostBinary -Destination $hostPath
    Copy-Item -LiteralPath $Proxy -Destination $proxyPath
    "[Graphics]`r`nEnableDX12=$DX12`r`nBorderless=1" | Set-Content -LiteralPath (Join-Path $testRoot ($prefix + '.ini')) -Encoding ASCII
    if ($NativeFocusHost) { & $hostPath $proxyPath $DX12 focus } else { & $hostPath $proxyPath $DX12 }
    if ($LASTEXITCODE -ne 0) { throw "Graphics host failed: $LASTEXITCODE" }
    Get-Content -LiteralPath (Join-Path $testRoot ($prefix + '.log'))
} finally {
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolvedTest.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase)) {
        foreach ($file in (Get-ChildItem -LiteralPath $resolvedTest -File)) { Remove-Item -LiteralPath $file.FullName }
        Remove-Item -LiteralPath $resolvedTest
    }
}
