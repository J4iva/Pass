# SPDX-License-Identifier: GPL-3.0-or-later
# Empaqueta pass.exe (release) en dist/ con todas sus DLLs.
#
# windeployqt6 copia las DLLs de Qt y sus plugins, pero NO el runtime de
# MinGW ni las librerías de terceros (icu, pcre2, zstd...); el walker de
# abajo cierra ese hueco siguiendo los imports con objdump.

$ErrorActionPreference = 'Stop'

$bin = 'C:\msys64\ucrt64\bin'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$exe = Join-Path $root 'build\release\app\pass.exe'

if (-not (Test-Path $exe)) {
    Write-Error "No existe $exe. Compila antes: cmake --build --preset release"
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory $dist | Out-Null
Copy-Item $exe $dist

$env:PATH = "$bin;$env:PATH"
& "$bin\windeployqt6.exe" --release --no-translations (Join-Path $dist 'pass.exe')

# Cierre transitivo de dependencias que vivan en ucrt64\bin.
$queue = @(Get-ChildItem $dist -Recurse -Include *.exe, *.dll)
$seen = @{}
while ($queue.Count -gt 0) {
    $next = @()
    foreach ($f in $queue) {
        $deps = & "$bin\objdump.exe" -p $f.FullName 2>$null |
            Select-String 'DLL Name: (.+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
        foreach ($d in $deps) {
            if ($seen.ContainsKey($d)) { continue }
            $seen[$d] = $true
            $src = Join-Path $bin $d
            $dst = Join-Path $dist $d
            if ((Test-Path $src) -and -not (Test-Path $dst)) {
                Copy-Item $src $dst
                $next += Get-Item $dst
            }
        }
    }
    $queue = $next
}

Write-Host "Listo: $dist ($((Get-ChildItem $dist -Recurse -File).Count) ficheros)"
