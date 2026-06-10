# Plan — "Pass": dashboard de productividad (Qt 6 Widgets + C++20)

## Contexto

Proyecto nuevo desde cero en `C:\Users\jaime\Pass` (directorio vacío). App de escritorio para estudiantes/trabajadores/investigadores con tres módulos integrados: **calendario** (local primero, Google Calendar en fase 2, extensible a otros proveedores), **notas integradas con Obsidian** (las notas se escriben como `.md` en el vault del usuario) y **gestor de sesiones de estudio** (pomodoros configurables, pausables, etiquetadas por asignatura, con estadísticas de horas y planificación en el calendario).

Decisiones cerradas con el usuario:
- **C++20 + Qt 6 (LTS 6.8+) con Qt Widgets** (no QML), build con **CMake**, Windows primero pero portable.
- **Proyecto completamente open source (licencia GPLv3)** y construido solo con herramientas open source: toolchain GCC/MinGW-w64 vía MSYS2, sin Visual Studio ni instalador propietario de Qt. IDEs: **Zed y/o VS Code** con clangd.
- **MVP 100% local**; Google Calendar (vía QtNetworkAuth / `QOAuth2AuthorizationCodeFlow`) en fase 2.
- Persistencia: **SQLite vía Qt SQL** para eventos/sesiones/asignaturas. Las notas NO van a la DB: la verdad es el sistema de ficheros (vault de Obsidian).

## Arquitectura

Dos targets + tests, con separación estricta para reutilizar la lógica en la futura versión web (detrás de Qt HttpServer/REST):

```
Pass/
├── CMakeLists.txt, CMakePresets.json, .clang-format, .gitignore, LICENSE (GPLv3)
├── estructura.md                  # OBLIGATORIO (instrucciones globales), en español, desde M0
├── core/                          # lib estática "passcore" — SOLO Qt::Core + Qt::Sql (NUNCA Widgets)
│   ├── include/pass/
│   │   ├── domain/   Subject, StudySession, PomodoroStrategy, CalendarEvent, Note
│   │   ├── db/       Database, Migrations (PRAGMA user_version, foreign_keys=ON)
│   │   ├── repo/     SubjectRepository, SessionRepository, EventRepository
│   │   ├── calendar/ CalendarProvider (interfaz abstracta) + LocalCalendarProvider
│   │   ├── notes/    VaultService, NoteSerializer, VaultWatcher
│   │   ├── session/  SessionTimerService, StrategyCatalog
│   │   ├── stats/    StatsService
│   │   └── settings/ AppSettings (QSettings)
│   └── src/ (espejo de include/)
├── app/                           # ejecutable Qt Widgets
│   ├── main.cpp, MainWindow (sidebar QListWidget + QStackedWidget)
│   ├── views/    Dashboard, Calendar, Notes, Study, Stats, SettingsDialog
│   ├── widgets/  TimerWidget, EventDialog, SessionSetupDialog, SubjectComboBox
│   ├── models/   EventListModel, SessionTableModel, NoteListModel
│   └── resources/ pass.qrc, icons/, app.rc
└── tests/                         # QtTest (QSignalSpy para timer/watcher) + ctest
```

Frontera core/UI: el core expone QObjects con señales; la UI conecta slots. La UI nunca toca `QSqlQuery` ni rutas de fichero.

## Modelo de datos (SQLite, `%APPDATA%/Pass/pass.db`)

- `subjects(id, name UNIQUE, color, archived)`
- `strategies(id, name, work_min, break_min, long_break_min, cycles_before_long, builtin)` — seed: 25/5, 45/5, 50/10
- `sessions(id, subject_id, strategy_id, topic, planned_min, actual_sec, started_at, ended_at, status planned|completed|aborted, event_id)`
- `events(id, provider DEFAULT 'local', external_id, title, description, start_utc, end_utc, all_day, rrule, subject_id, source_session_id, updated_at)` + índices por `start_utc` y `subject_id`

Fechas **siempre en UTC ISO 8601**; conversión a local solo en la UI. `external_id` y `provider` ya preparan la sincronización con Google.

## Diseño por módulos (claves)

1. **Calendario**: interfaz `CalendarProvider` (`eventsBetween`, `addEvent`, `updateEvent`, `removeEvent`, señal `eventsChanged`). MVP con `QCalendarWidget` + lista de eventos del día + `EventDialog`. Vista mensual custom queda post-MVP.
2. **Notas/Obsidian**:
   - `VaultService` escribe con `QSaveFile` (atómico) en `vault/subfolder` (default `Pass/`) configurado en `SettingsDialog`; validar `.obsidian/` solo con aviso.
   - Nombre de fichero: `YYYY-MM-DD HHmm - <título-saneado>.md` (sanear `\/:*?"<>|`, colisiones con sufijo).
   - Frontmatter YAML propio mínimo (created, app, subject, session, tags) con **round-trip de claves desconocidas** (parser propio, sin libyaml).
   - `VaultWatcher` (QFileSystemWatcher + debounce ~500 ms + re-add tras rename-on-save de Obsidian). Conflicto con cambios locales sin guardar → barra "Recargar / Conservar mío" (sin auto-merge en MVP).
3. **Sesiones de estudio**:
   - `SessionTimerService`: máquina de estados con enum (`Idle → Running ⇄ Paused`, `Running → Break → Running`, `→ Finished/Aborted`). Tiempo real con `QElapsedTimer` + timestamps (el QTimer de 1 s es solo refresco de UI; sobrevive a suspensión del PC).
   - `StrategyCatalog`: función pura que, dada una duración (p. ej. 120 min), propone planes ("2×(50+10)", "4×(25+5)").
   - "Planificar para más tarde" crea `CalendarEvent` + sesión `planned`; desde el evento se puede lanzar la sesión.
4. **Estadísticas**: `StatsService` con agregados SQL (horas/asignatura, últimos 30 días). UI con QtCharts si el instalador lo trae sin fricción; fallback: barras con QPainter (trivial para 2 gráficos).

## Orden de implementación (cada milestone ejecutable)

1. **M0 — Esqueleto**: **copiar este plan completo a `docs/plan.md` dentro del proyecto** (primer paso, antes de nada), `LICENSE` GPLv3, CMake multi-target, presets GCC/Ninja (MSYS2 UCRT64) con `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, MainWindow con sidebar y páginas placeholder, `.clang-format`, `git init`, `estructura.md` inicial. ✓ compila y abre ventana.
2. **M1 — Núcleo de datos**: Database + migración v1 + repos Subject/Strategy con seed; tests con DB `:memory:`. ✓ ctest en verde.
3. **M2 — Sesiones**: StrategyCatalog + SessionTimerService + StudyView/TimerWidget/SessionSetupDialog; persistir sesiones. Tests con QSignalSpy. ✓ sesión de 1 min visible en DB.
4. **M3 — Estadísticas**: StatsService + StatsView (horas/asignatura + historial en QTableView). ✓ reflejan sesiones de M2.
5. **M4 — Calendario local**: provider + CalendarView + EventDialog + planificar/lanzar sesiones desde eventos. ✓ CRUD de eventos funcionando.
6. **M5 — Notas/Obsidian**: NoteSerializer (tests de round-trip primero) + VaultService + NotesView con autosave (debounce 1-2 s) + config de vault. ✓ nota creada en la app se abre bien en Obsidian.
7. **M6 — Watcher y pulido**: VaultWatcher + conflictos, DashboardView resumen, icono. ✓ editar en Obsidian refresca la app abierta.
8. **M7 — Empaquetado**: `windeployqt`, smoke test, `estructura.md` final.

**Fase 2 (posterior, fuera de este plan)**: `GoogleCalendarProvider` con QOAuth2AuthorizationCodeFlow + mapeo `external_id`/sync tokens. ⚠️ **Seguridad**: al implementarla se manejarán credenciales OAuth y almacenamiento de tokens → avisar al usuario y entrar en modo **contingencia** (skill `contingencia`) en ese momento.

## Tooling y requisitos previos (100% open source)

- **MSYS2** (entorno UCRT64) como base de toda la toolchain, instalado con pacman:
  `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,clang-tools-extra,gdb,qt6-base,qt6-charts,qt6-tools}` (clang-tools-extra aporta clangd y clang-format). Todo libre: ni Visual Studio, ni MSVC, ni cuenta de Qt. (aqtinstall, también OSS, reservado para CI futura.)
- **IDE: Zed y/o VS Code con clangd** (en VS Code: extensiones clangd + CMake Tools; en Zed clangd va integrado). `CMAKE_EXPORT_COMPILE_COMMANDS=ON` en los presets para que clangd funcione; añadir symlink/copia de `compile_commands.json` a la raíz.
- CMakePresets con Ninja y generador para el entorno UCRT64 de MSYS2 (`CMAKE_PREFIX_PATH` apuntando a `C:/msys64/ucrt64`), AUTOMOC/AUTORCC/AUTOUIC ON. Debug con gdb.
- Despliegue: `windeployqt6 --release pass.exe` (incluido en qt6-tools de MSYS2).
- **Licencia GPLv3**: archivo `LICENSE` en la raíz desde M0 y cabecera SPDX (`SPDX-License-Identifier: GPL-3.0-or-later`) en los fuentes.

## Riesgos clave

- **Licencias**: la app es GPLv3, plenamente compatible con Qt LGPLv3/GPL; enlazado dinámico de Qt (lo que windeployqt produce por defecto). `passcore` estático es código propio, sin problema. Verificar que QtCharts en MSYS2 llega bajo GPLv3 (lo es) — compatible al ser nuestra app GPLv3.
- **Pureza del core**: enlazar solo `Qt6::Core Qt6::Sql` en su target — el linker falla si alguien cuela un widget.
- **DST/zonas horarias**: guardar instantes UTC, no fecha+hora local; all-day como fecha pura.
- **QFileSystemWatcher en Windows**: duplicados y pérdida de watch tras rename — debounce + re-add + vigilar también el directorio.
- **Vault movido/borrado**: modo degradado del módulo de notas con aviso, nunca crash.

## Verificación

- Por milestone: criterio "✓" de cada uno + `ctest` en verde (migraciones, repos, timer con QSignalSpy, round-trip del serializer).
- E2E final del MVP: crear asignatura → planificar sesión de estudio (aparece en el calendario) → ejecutarla con pomodoro pausándola una vez → ver horas en estadísticas → crear nota desde la sesión → abrirla en Obsidian y editarla allí → comprobar que la app la refresca.
- Empaquetado: ejecutar el `pass.exe` desplegado con windeployqt6 en una ruta limpia sin MSYS2/Qt en el PATH.
