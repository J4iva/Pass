# Pass — estructura del proyecto

## Qué resuelve

Dashboard de escritorio para estudiantes, trabajadores e investigadores que integra
calendario, notas sincronizadas con Obsidian y un gestor de sesiones de estudio
(pomodoros configurables con estadísticas de horas por asignatura). Pensado para,
en el futuro, sincronizar con Google Calendar y tener versión web.

## Tecnologías

- **C++20** con **Qt 6 (Qt Widgets)** — UI de escritorio nativa.
- **SQLite vía Qt SQL** — persistencia de eventos, sesiones y asignaturas.
- **Markdown + YAML frontmatter** — las notas viven como ficheros `.md` en el vault
  de Obsidian (el sistema de ficheros es la fuente de verdad, no la base de datos).
- **CMake ≥ 3.25** con presets — build multi-target.
- Licencia: **GPLv3** (ver `LICENSE`).

## Estructura del proyecto

```
Pass/
├── CMakeLists.txt        # proyecto raíz: targets core, app y tests
├── CMakePresets.json     # presets debug/release (GCC + Ninja, MSYS2 UCRT64)
├── estructura.md         # este documento
├── docs/plan.md          # plan de implementación completo (milestones M0-M7)
├── scripts/deploy.ps1    # empaqueta release en dist/ (windeployqt6 + runtime MinGW)
├── core/                 # lib estática "passcore": lógica de negocio
│   │                     #   SOLO Qt Core + Qt Sql (nunca Widgets), reutilizable
│   │                     #   en la futura versión web detrás de un servidor REST
│   ├── include/pass/     # headers públicos por módulo:
│   │                     #   domain/ db/ repo/ calendar/ notes/ session/ stats/ settings/
│   └── src/              # implementación (espejo de include/)
├── app/                  # ejecutable Qt Widgets
│   ├── MainWindow.*      # shell: sidebar de navegación + QStackedWidget
│   ├── views/            # Dashboard, Calendario, Notas, Estudio, Estadísticas
│   ├── widgets/          # timer, diálogos de sesión y de evento
│   └── models/           # modelos Qt (QAbstractItemModel) que consumen el core
└── tests/                # 10 suites QtTest registradas en ctest
```

Estado actual: MVP completo (M0-M7 del plan). Pendiente fase 2: sincronización
con Google Calendar (QtNetworkAuth) — al implementarla, revisar seguridad de
credenciales/tokens (modo contingencia).

## Decisiones clave

- **Qt Widgets en vez de QML**: UI 100% en C++ (lenguaje que domina el equipo),
  más productivo para una app tipo dashboard; QML habría exigido aprender un
  lenguaje declarativo extra.
- **C++/Qt en vez de Java/JavaFX o Electron**: preferencia del autor por C++;
  Qt aporta SQLite, OAuth2 (QtNetworkAuth, para la fase de Google Calendar),
  watcher de ficheros y empaquetado, todo open source.
- **Core separado de la UI**: `passcore` no puede enlazar Qt Widgets; así la
  lógica se reutiliza tal cual en una futura versión web (Qt HttpServer/REST).
- **SQLite y no un ORM**: una sola dependencia (driver QSQLITE incluido en Qt),
  consultas preparadas a mano; migraciones versionadas con `PRAGMA user_version`.
- **Notas como ficheros, no en DB**: un vault de Obsidian es una carpeta de
  Markdown; escribir `.md` directamente garantiza compatibilidad total.
- **QtTest en vez de Catch2/GTest**: viene con Qt (cero dependencias extra) y
  aporta `QSignalSpy`/event loop, necesarios para testear timers y watchers.
- **Toolchain 100% open source**: GCC (MinGW-w64) + CMake + Ninja vía MSYS2;
  se descartó MSVC/Visual Studio (propietario) y el instalador online de Qt
  (requiere cuenta). Fechas siempre en UTC ISO 8601; conversión a local solo en UI.

## Herramientas

- **Build**: CMake + Ninja con presets (`cmake --preset debug && cmake --build --preset debug`).
- **Toolchain**: MSYS2 UCRT64 (`C:\msys64\ucrt64`): GCC, gdb, Qt 6, clangd, clang-format.
- **IDE**: Zed y/o VS Code con clangd (usa el `compile_commands.json` generado por CMake).
- **Tests**: QtTest + `ctest --preset debug`.
- **Formato**: clang-format (config en `.clang-format`, estilo LLVM ajustado).
- **Despliegue**: `scripts/deploy.ps1` → carpeta `dist/` autocontenida. Usa
  `windeployqt6` (DLLs y plugins de Qt) más un walker con objdump que copia el
  runtime de MinGW y terceros (icu, pcre2, zstd...), que windeployqt no cubre.
  Qt va enlazado dinámicamente (requisito LGPL).
