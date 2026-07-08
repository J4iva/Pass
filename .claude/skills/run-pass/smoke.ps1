# SPDX-License-Identifier: GPL-3.0-or-later
# Driver de Pass: RECOMPILA -> lanza -> captura pantalla -> verifica titulo -> cierra.
#
# Uso (desde la raiz del repo, C:\Users\jaime\Pass):
#   pwsh -File .claude\skills\run-pass\smoke.ps1            # recompila debug, lanza, captura
#   pwsh -File .claude\skills\run-pass\smoke.ps1 -Config release
#   pwsh -File .claude\skills\run-pass\smoke.ps1 -NoBuild   # solo lanza (NO recomendado tras tocar codigo)
#   pwsh -File .claude\skills\run-pass\smoke.ps1 -BuildOnly # solo recompila + ctest, sin lanzar GUI
#
# Devuelve exit 0 si la ventana abre con el titulo esperado; !=0 si falla.

param(
    [ValidateSet('debug', 'release')] [string]$Config = 'debug',
    [switch]$NoBuild,    # saltar la recompilacion (por defecto SIEMPRE recompila)
    [switch]$BuildOnly,  # recompilar (+ctest en debug) y salir sin abrir la GUI
    [int]$WaitSeconds = 5,
    [string]$Shot = ''
)

$ErrorActionPreference = 'Stop'
$ucrt = 'C:\msys64\ucrt64\bin'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$env:PATH = "$ucrt;$env:PATH"   # Qt6*.dll + gcc/cmake/ninja/ctest viven aqui
if ($Shot -eq '') { $Shot = Join-Path $PSScriptRoot 'last-run.png' }

Push-Location $root
try {
    # --- 1. RECOMPILAR (el motivo de esta skill: tras cualquier cambio de codigo) ---
    if (-not $NoBuild) {
        Write-Host "==> Recompilando ($Config)..." -ForegroundColor Cyan
        & cmake --preset $Config | Out-Null
        & cmake --build --preset $Config
        if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion ($Config)." }
        Write-Host "    build OK" -ForegroundColor Green

        if ($Config -eq 'debug') {
            Write-Host "==> ctest..." -ForegroundColor Cyan
            & ctest --preset debug --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw 'Tests en rojo.' }
            Write-Host "    tests OK" -ForegroundColor Green
        }
    }
    if ($BuildOnly) { Write-Host 'BuildOnly: hecho.' -ForegroundColor Green; exit 0 }

    # --- 2. LANZAR la GUI ---
    $exe = Join-Path $root "build\$Config\app\pass.exe"
    if (-not (Test-Path $exe)) { throw "No existe $exe (compila primero)." }
    Write-Host "==> Lanzando $exe" -ForegroundColor Cyan
    $p = Start-Process -FilePath $exe -PassThru
    Start-Sleep -Seconds $WaitSeconds

    if ($p.HasExited) { throw "pass.exe salio con codigo $($p.ExitCode) (DLL/crash?)." }
    $p.Refresh()
    $title = $p.MainWindowTitle
    # OJO: un dialogo de error de DLL tambien mantiene vivo el proceso; por eso
    # verificamos el titulo de la ventana, no solo que el proceso siga vivo.
    if ([string]::IsNullOrWhiteSpace($title)) { throw 'La ventana no tiene titulo (posible dialogo de error).' }

    # --- 3. CAPTURA de la ventana ---
    # Usamos PrintWindow (PW_RENDERFULLCONTENT) en vez de CopyFromScreen: captura
    # ESA ventana aunque este detras de otras. SetForegroundWindow desde un proceso
    # de fondo lo bloquea Windows, asi que CopyFromScreen capturaria lo que este encima.
    try {
        Add-Type -AssemblyName System.Drawing
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
        $r = New-Object Win+RECT
        [Win]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
        if ($w -gt 0 -and $h -gt 0) {
            $bmp = New-Object System.Drawing.Bitmap $w, $h
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $hdc = $g.GetHdc()
            # flags=2 => PW_RENDERFULLCONTENT (necesario para apps con render acelerado)
            [Win]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null
            $g.ReleaseHdc($hdc)
            $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
            $g.Dispose(); $bmp.Dispose()
            Write-Host "    captura -> $Shot" -ForegroundColor Green
        }
    } catch {
        Write-Warning "No se pudo capturar pantalla: $_"
    }

    # --- 4. CERRAR ---
    Stop-Process -Id $p.Id -Force
    Write-Host "OK: ventana '$title' abierta y verificada." -ForegroundColor Green
    exit 0
} finally {
    Pop-Location
}
