# Pass — estructura del proyecto

## Qué resuelve

Dashboard de escritorio para estudiantes, trabajadores e investigadores que integra
calendario (con **sincronización bidireccional con Google Calendar**), notas
sincronizadas con Obsidian, un gestor de sesiones de estudio (pomodoros
configurables con estadísticas de horas por asignatura) y **sincronización entre
dispositivos** de datos y notas a través de un repositorio privado de GitHub.
Pensado para tener una versión web en el futuro.

## Tecnologías

- **C++20** con **Qt 6 (Qt Widgets)** — UI de escritorio nativa.
- **SQLite vía Qt SQL** — persistencia de eventos, sesiones y asignaturas.
- **Qt NetworkAuth / Qt Network** — OAuth2 (Authorization Code + PKCE) y REST con
  Google Calendar (v6.11.1, MSYS2 UCRT64).
- **Administrador de credenciales de Windows (wincred/advapi32)** — almacén seguro
  de tokens y client_id/secret de Google (nunca en disco ni en el repo).
- **Markdown + YAML frontmatter** — las notas viven como ficheros `.md` en el vault
  de Obsidian (el sistema de ficheros es la fuente de verdad, no la base de datos).
- **git del sistema (vía QProcess) + Git Credential Manager** — sincronización entre
  dispositivos contra un repo privado de GitHub; la app nunca maneja credenciales.
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
├── docs/google-calendar.md # guía (ES) para crear el OAuth Client + checklist seguridad
├── docs/github-sync.md     # guía (ES) de sincronización entre dispositivos + seguridad
├── docs/passport-integration.md # spec: cómo PassPort (asistente móvil) escribe en PASS vía el repo de sync
├── core/                 # lib estática "passcore": lógica de negocio
│   │                     #   Qt Core/Sql/Network/NetworkAuth (nunca Widgets/Gui),
│   │                     #   reutilizable en la futura versión web tras un REST
│   ├── include/pass/     # headers públicos por módulo:
│   │                     #   domain/ db/ repo/ admin/ calendar/ google/ notes/ session/ stats/ settings/ sync/ command/
│   └── src/              # implementación (espejo de include/)
│                         #   google/: TokenStore+WinCred, GoogleAuthService (OAuth2/PKCE),
│                         #   GoogleEventMapper, CalendarClient+GoogleCalendarClient,
│                         #   GoogleSyncService; calendar/CalendarService (router write-through);
│                         #   admin/SubjectAdminService (cascada al renombrar/eliminar asignaturas);
│                         #   command/: CommandParser (CLI estricto) + CommandProcessor
│                         #   (materializa `create subject/topic/note/event/task/session`
│                         #   con UUIDv5 del texto del comando; ver docs/commands.md);
│                         #   sync/: DataChangeNotifier, Tombstones, SyncSerializer,
│                         #   SyncExporter/SyncImporter (espejo JSON + LWW), GitRunner
│                         #   (QProcess seguro), GitSyncService (pipeline) y
│                         #   GitSyncController (fachada que lo corre en un hilo worker)
├── app/                  # ejecutable Qt Widgets
│   ├── MainWindow.*      # shell: sidebar de navegación + QStackedWidget
│   ├── views/            # Dashboard, Calendario, Notas, Estudio, Estadísticas, Ajustes,
│   │                     #   SubjectAdminView (Administración de asignaturas/temas)
│   ├── widgets/          # timer, diálogos de sesión, de evento y de credenciales de Google
│   ├── util/             # helpers de UI compartidos (asignaturas, planificar sesión)
│   └── models/           # modelos Qt (QAbstractItemModel) que consumen el core
└── tests/                # 18 suites QtTest registradas en ctest
```

Estado actual: MVP (M0-M7) + fase 2 de Google Calendar (hitos G1-G5) + fase 3 de
sincronización entre dispositivos (M8.1-M8.6) completos. La integración con Google y
la sincronización por GitHub requieren validación E2E manual con cuentas reales (ver
`docs/google-calendar.md` y `docs/github-sync.md`); la sync entre dispositivos tiene
además tests E2E automáticos contra un repo bare local (`tst_gitsyncservice` para el
pipeline y `tst_gitsynccontroller` para el camino en hilo worker).

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
- **Google Calendar con OAuth2 + PKCE S256 (modo contingencia)**: flujo de
  Authorization Code con `QtNetworkAuth`, redirect loopback solo `127.0.0.1` con
  puerto efímero, scope mínimo `calendar.events`. Tokens y client_id/secret en el
  **Administrador de credenciales de Windows** (descartado QSettings/ficheros por
  exponer secretos). La app no trae credenciales: cada usuario crea su OAuth Client
  (PKCE es la defensa real; el "secret" de cliente Desktop no es confidencial).
- **Modelo write-through + espejo**: Google es la fuente de verdad; los eventos se
  espejan en la tabla `events` (columnas `provider/external_id/etag`) y las
  mutaciones sobre eventos de Google se aplican al instante por API con `etag`
  (If-Match), revirtiendo en local si fallan. Sync incremental con `syncToken`
  (410 → full resync). `CalendarService` enruta local vs Google sin tocar las vistas.
- **Ajustes divididos en Conexiones y Administración**: la pantalla de Ajustes
  empezaba a saturarse, así que se separa con una lista lateral interna en dos
  apartados. **Conexiones** agrupa las integraciones **por tecnología** (hoy Google
  Calendar y repo de GitHub; el patrón admite añadir más) — credenciales de Google
  Cloud en un pop-up (`GoogleCredentialsDialog`), estado de cuenta y de sync, y para
  GitHub el clonado/adopción, nombre de dispositivo y estado. **Administración**
  gestiona los datos propios: asignaturas y sus temas. Se eligió lista lateral (en
  vez de pestañas) por coherencia con el sidebar principal y para que cada tecnología
  crezca en su propio grupo.
- **Temas como entidad (tabla `topics`, migración v4)**: antes un "tema" era solo
  texto libre en sesiones/notas; para poder gestionarlos desde Administración pasan a
  ser una entidad propia con `UNIQUE(subject_id, name)`. Se sincroniza como el resto
  (un JSON por registro con `updated_at` + tombstones); la sync es **aditiva** y
  compatible hacia atrás (una versión antigua ignora `data/topics/`), por lo que **no**
  se sube `manifest.format`. Renombrar una asignatura no toca eventos/sesiones (la
  referencian por `subject_id`, así que su nombre mostrado cambia solo); sí reescribe
  las notas `.md`, que llevan la asignatura embebida (frontmatter, tag y nombre de
  fichero), vía `SubjectAdminService`. Eliminar una asignatura borra sus tareas ([T]),
  temas y notas, y conserva el histórico dejando sesiones/eventos sin asignatura.
- **Tareas como convención sobre eventos**: una tarea es un evento cuyo título
  empieza por `[T]` (marcador visible también en Google Calendar, sin columnas
  nuevas ni problemas de sync) con asignatura obligatoria; su inicio es la fecha
  de entrega. Las sesiones se vinculan a una tarea vía `sessions.event_id` para
  acumular horas trabajadas (helpers `isTask`/`taskDisplayTitle` en `CalendarEvent.h`).
- **Planificar vs. empezar sesiones, separado por vista**: planificar una sesión
  (crear `StudySession` en estado `Planned` + su evento de calendario) se hace tanto
  desde **Calendario** ("Nueva sesión", solo planifica) como desde **Sesiones**; pero
  **empezarla** (arrancar el timer) solo se puede desde **Sesiones**, que lista las
  planificadas pendientes con un botón "Empezar". La lógica común vive en
  `app/util/SessionPlanner` para no duplicarla entre ambas vistas.
- **Retomar sesiones interrumpidas**: al cerrar la app con una sesión en marcha,
  `interrupt()` guarda su posición exacta (fase + segundos consumidos) en las columnas
  `resume_phase_index`/`resume_phase_elapsed_sec` (migración V5) reutilizando el estado
  `Aborted`. Una sesión es **retomable** si está abortada y conserva esa posición con
  tiempo restante; aparece en *Sesiones pendientes* como "⏸ Reanudar" y al lanzarla el
  timer reconstruye las fases (`SessionTimerService::phasesFor` desde estrategia +
  minutos) y continúa donde se quedó. Terminar a mano es definitivo (sin posición). Las
  dos columnas viajan en la sync como campos opcionales (retrocompatibles).
- **Sincronización entre dispositivos con repo de GitHub (modo contingencia)**: se
  eligió un repo **privado** de GitHub como backend (lo más simple: sin servidor
  propio) frente a un servicio en la nube a medida. El transporte es `git` del
  sistema vía `QProcess` (programa + lista de args, **nunca** shell) y las
  credenciales las gestiona **Git Credential Manager** (la app jamás las ve). Los
  datos se espejan como **un JSON por registro** con `updated_at` (diffs limpios y
  *last-writer-wins* por registro, sin locking) y los borrados van por *tombstones*
  explícitos (nunca "archivo ausente = borrado"); así la concurrencia entre dos
  dispositivos no corrompe datos. UUIDv5 deterministas para las estrategias builtin
  (V3) para que las sesiones referencien la misma estrategia en cualquier equipo.
  Seguridad: whitelist de URLs (`isAllowedRemoteUrl`), timeouts con kill,
  `GIT_TERMINAL_PROMPT=0`, salida saneada (`redacted`) y validación estricta de la
  entrada remota en el importador (rutas, tamaño, tipos, guard de `manifest.format`).
  Las **notas** no van en la BD: Pass espeja su subcarpeta de notas del vault ⇄
  `notes/` del repo (unión inicial aditiva + espejo gobernado por git, conflictos =
  conservar ambas), así cada dispositivo conserva su propio vault de Obsidian y solo
  comparte las notas de Pass. Detalle en `docs/github-sync.md`.
- **El pipeline de sync corre en un hilo worker** (`GitSyncController`): la red git +
  el import/export SQL se sacan del hilo de la GUI para que la interfaz **nunca se
  congele** ante un remoto lento u hostil. El worker (`GitSyncService`) abre **su
  propia conexión SQLite** al mismo fichero; la BD está en modo **WAL** + `busy_timeout`
  para que el lector (GUI) y el escritor (worker) coexistan sin `SQLITE_BUSY` ni
  carreras. La fachada cachea el estado y reenvía toda llamada al worker en cola; el
  cierre es acotado (push final con presupuesto). Cómo y por qué probarlo: skill
  `pruebas-concurrencia-sync` + `tst_gitsynccontroller`.
- **Endurecimiento del binario (modo contingencia)**: el `CMakeLists.txt` raíz añade,
  en GCC/Clang, `-Wall -Wextra -Wformat -Wformat-security -fstack-protector-strong`,
  `_FORTIFY_SOURCE=2` (solo Release) y flags de enlazado ASLR/DEP/high-entropy-va en
  Windows (verificado: `pass.exe` con `DllCharacteristics 0x0160`). Sin `-Werror` para
  no romper el build por warnings preexistentes. Defensa en profundidad sobre el código
  que parsea entrada semi-confiable (JSON de sync/Google, `.md` del vault).
- **CLI por comando en el repo de sync (modo contingencia)**: una carpeta `command/`
  del repo donde se escriben comandos en texto estilo CLI (`Pass create note "..."`).
  El `GitSyncService` los procesa tras cada *pull* (y al arrancar la app) en el hilo
  worker, reutilizando los repositorios del core y el espejo de notas: el efecto viaja
  por el mismo canal ya endurecido (LWW + validación del `SyncImporter`). Solo `create`
  (nunca borrado). **Idempotencia** por UUIDv5 del texto del comando: reproducir el
  mismo `.passcmd` (aquí o en otro dispositivo) produce el mismo id y se deduplica; las
  notas llevan `pass_command_id` en el frontmatter como marca. Al aplicarse, el fichero
  se mueve a `command/processed/` en el mismo commit/push para que ningún dispositivo lo
  reprocese. Parser de **lista blanca** (acción/entidad/flags contra conjuntos fijos),
  límites de tamaño (8 KiB) y número (200/ciclo), fallos aislados (un comando roto no
  aborta el ciclo) y toggle `commands/enabled` en Ajustes. Spec completa:
  `docs/commands.md`; tests en `tst_commandparser`, `tst_commandprocessor` (unitarios) y
  `tst_commands` (E2E multi-dispositivo real con git).

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
