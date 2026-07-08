---
name: run-pass
description: >
  Recompila, ejecuta y verifica la app de escritorio Pass (C++/Qt6, Windows/MSYS2).
  USAR SIEMPRE DESPUÉS DE CUALQUIER CAMBIO DE CÓDIGO: hay que recompilar para que el
  cambio tenga efecto (la GUI no recarga sola). También para: build, compilar, rebuild,
  recompilar, lanzar/run/ejecutar/abrir Pass, captura/screenshot de la ventana, pasar
  los tests (ctest), o empaquetar (deploy) la app.
---

# run-pass — recompilar y ejecutar Pass

Pass es una app de escritorio **C++20 + Qt6 Widgets** (SQLite, QtNetworkAuth para Google
Calendar) que se compila con **CMake + Ninja** bajo el toolchain **MSYS2 UCRT64**. No es
una web ni un servidor: es una GUI nativa de Windows.

> ⚠️ **Tras tocar cualquier `.cpp`/`.h`/`CMakeLists.txt` hay que RECOMPILAR.** La ventana
> abierta no refleja los cambios hasta que reconstruyes el binario. El driver de esta
> skill recompila siempre por defecto.

Todas las rutas son relativas a la raíz del repo (`C:\Users\jaime\Pass`).
El driver es **`.claude/skills/run-pass/smoke.ps1`** (PowerShell 7 / `pwsh`).

## Requisitos

Toolchain MSYS2 UCRT64 en `C:\msys64\ucrt64`. Si falta el paquete de OAuth (Google
Calendar), instálalo con pacman:

```bash
/c/msys64/usr/bin/pacman.exe -S --needed --noconfirm mingw-w64-ucrt-x86_64-qt6-networkauth
```

`C:\msys64\ucrt64\bin` **debe** estar en el PATH al compilar y al ejecutar (ahí viven
`gcc`, `cmake`, `ninja`, `ctest` y todas las `Qt6*.dll`). El driver lo añade solo.

## Recompilar + ejecutar (camino del agente) — ESTO ES LO PRINCIPAL

Un solo comando recompila (debug), pasa los tests, lanza la GUI, captura la ventana y la
cierra verificando el título:

```powershell
pwsh -File .claude\skills\run-pass\smoke.ps1
```

Salida esperada (resumen): `build OK` → `tests OK` → `captura -> ...last-run.png` →
`OK: ventana 'Pass 0.1.0' abierta y verificada.` La captura queda en
`.claude/skills/run-pass/last-run.png` — **ábrela y míra**la para confirmar la UI.

Variantes:

```powershell
pwsh -File .claude\skills\run-pass\smoke.ps1 -Config release   # build release + lanzar
pwsh -File .claude\skills\run-pass\smoke.ps1 -BuildOnly         # solo recompilar + ctest, sin GUI
pwsh -File .claude\skills\run-pass\smoke.ps1 -NoBuild           # solo lanzar (NO tras cambiar código)
```

## Recompilar a mano (sin el driver)

Desde un shell con `C:\msys64\ucrt64\bin` en el PATH:

```bash
cmake --preset debug          # idempotente; configura build/debug
cmake --build --preset debug  # <-- recompila tras cada cambio
ctest --preset debug          # 13 suites, deben quedar 13/13 verdes
```

Release + empaquetado autocontenido en `dist/`:

```bash
cmake --build --preset release
```
```powershell
& .\scripts\deploy.ps1        # windeployqt6 + walker objdump -> dist\ (54 ficheros)
```

## Ejecutar a mano (camino humano)

```bash
./build/debug/app/pass.exe    # abre la ventana; ciérrala tú
```
Útil solo en un escritorio interactivo; headless no sirve.

## Gotchas (cosas que mordieron de verdad)

- **La GUI no recarga sola.** Editar código y volver a abrir la ventana sin recompilar
  muestra el binario viejo. Recompila siempre (es el motivo de esta skill).
- **PATH sin ucrt64 → `0xC0000139` (entry point not found)** o no arranca: faltan las
  `Qt6*.dll`. Añade `C:\msys64\ucrt64\bin` al PATH.
- **Smoke test con falso positivo:** un diálogo de error de DLL mantiene el proceso vivo.
  No basta con que el proceso exista; verifica `MainWindowTitle` (el driver ya lo hace).
- **Captura de la ventana:** usa `PrintWindow` con `PW_RENDERFULLCONTENT` (flag 2), NO
  `CopyFromScreen`. Desde un proceso de fondo, `SetForegroundWindow` está bloqueado por
  Windows, así que `CopyFromScreen` capturaría la ventana que esté encima (terminal),
  no Pass. El driver ya usa `PrintWindow`.
- **clang-format reordena los includes de Windows** y rompe `WinCredTokenStore.cpp`:
  `<windows.h>` DEBE ir antes que `<wincred.h>`. Esos includes van entre
  `// clang-format off` / `// clang-format on`. No los toques.
- **moc se atraganta con literales en crudo `R"(...)"` dentro de clases `Q_OBJECT`**
  ("No relevant classes found" → error de link por la vtable). En los tests, el JSON va
  como literales normales con comillas escapadas, no `R"(...)"`.
- **Secretos de Google** (tokens, client_id/secret) viven SOLO en el Administrador de
  credenciales de Windows. `tst_tokenstore` usa un prefijo único por ejecución para no
  tocar credenciales reales; no lo "limpies" hacia QSettings/ficheros.

## Troubleshooting

| Síntoma | Causa / arreglo |
|---|---|
| `Could not find ... qt6-networkauth` al configurar | Falta el paquete; ejecuta el `pacman -S` de Requisitos. |
| `0xC0000139` / la ventana no abre | `C:\msys64\ucrt64\bin` no está en el PATH. |
| El driver dice "La ventana no tiene título" | Diálogo de error (p. ej. DLL): mira la consola y revisa el PATH/deps. |
| `last-run.png` sale en negro | App con render acelerado y `PrintWindow` sin flag 2; el driver ya pasa `2`. |
| `cmake --preset` falla por compilador | Lanza desde un shell con el PATH de ucrt64 (o usa el driver, que lo pone). |
