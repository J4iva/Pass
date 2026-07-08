# Tareas

## Fase 3 — Sincronización entre dispositivos (repo privado de GitHub)

Modo contingencia activo durante toda la fase (credenciales, ejecución de procesos,
datos personales en un remoto). Ver el plan completo y la guía `docs/github-sync.md`.

- [x] **M8.1 — Base de datos lista para sync**: migración V3 (`updated_at` en
      subjects/strategies/sessions + backfill, tabla `tombstones`, UUIDv5
      deterministas para las estrategias builtin con cascada). Repos con `updated_at`
      y tombstones al borrar; `DataChangeNotifier`. Tests en `tst_migrations` y
      `tst_repositories`.
- [x] **M8.2 — Espejo JSON**: `SyncSerializer` (entidad↔JSON con validación),
      `SyncExporter` (BD→`data/`, QSaveFile, borra obsoletos), `SyncImporter`
      (LWW, tombstones, resurrección, colisión de nombres de asignatura, resolución
      de eventos por `external_id`, validación de entrada). Tests `tst_syncserializer`
      y `tst_syncimportexport`.
- [x] **M8.3 — GitRunner (QProcess seguro)**: ejecución async + `runBlocking`,
      programa+args sin shell, env endurecido, timeouts con kill, `isAllowedRemoteUrl`
      (whitelist) y `redacted`. Test `tst_gitrunner`.
- [x] **M8.4 — GitSyncService (orquestador)**: pipeline preflight → export+presencia
      → commit → fetch → merge (con resolución de conflictos por archivo) → import →
      re-export → push (con reintento no-ff); clone/adopt, `checkRepoIsPrivate`,
      presencia y `shutdownSync`. Test E2E `tst_gitsyncservice` (repo bare + 2 clones).
- [x] **M8.5 — UI e integración**: claves de sync en `AppSettings`, grupo
      "Sincronización entre dispositivos (GitHub)" en `SettingsView`, integración en
      `MainWindow` (arranque, refresco de vistas con `remoteDataApplied`, aviso de
      presencia, `closeEvent` → `shutdownSync`).
- [x] **M8.6 — Docs y cierre**: `docs/github-sync.md`, actualización de
      `estructura.md` y `tasks.md`. Git es dependencia **opcional** de runtime: si
      falta, la sección de sync queda *deshabilitada* y el resto de Pass funciona.

### Pendiente / seguimiento

- [x] **M9 — CLI por comando (carpeta `command/` del repo de sync)**: pseudo-API para
      crear datos sin abrir la app. Solo `create` (subject/topic/note/event/task/session),
      nunca borrado. Modo contingencia (entrada externa parseada y aplicada automáticamente).
  - [x] **M9.1 — Parser + tipos + UUIDv5**: `CommandTypes`, `CommandId`
        (UUIDv5 determinista del texto, namespace dedicado) y `CommandParser` estricto
        (lista blanca de acción/entidad/flags; tokenizador shell sin eval). Test
        `tst_commandparser`.
  - [x] **M9.2 — `create subject/topic`**: `CommandProcessor` + resolución por nombre
        (`byName`/`bySubjectAndName` en los repos). Idempotencia por UUIDv5 + reutilización
        si ya existe. Test `tst_commandprocessor`.
  - [x] **M9.3 — `create event/task/session`**: fechas ISO (Z=UTC, sin Z=local);
        `task` = evento `[T]` con asignatura obligatoria; `session` crea evento+sión
        enlazados en transacción. Idempotencia con `deterministicIdFor(salt)`.
  - [x] **M9.4 — `create note`**: escribe el `.md` en `repo/notes` (reusa `NoteSerializer`
        + `VaultService::sanitizeTitle`); marca `pass_command_id` en frontmatter para
        idempotencia (las notas no tienen UUID).
  - [x] **M9.5 — Hook en el pipeline + toggle**: `processPendingCommands()` en
        `GitSyncService` (hilo worker) en los 2 puntos (post-merge y primer ciclo),
        antes del espejo de notas para que no se borren. Límites 8 KiB / 200 por ciclo;
        fallos aislados. Toggle `commands/enabled` en `AppSettings` cableado vía
        `GitSyncController` y `MainWindow`.
  - [x] **M9.6 — Tests E2E + docs**: `tst_commands` (multi-dispositivo real con git:
        propagación, nota al vault del otro equipo, comando malformado que no rompe el
        ciclo, toggle off); `docs/commands.md` (spec pública); `estructura.md` y este
        fichero actualizados.

### Pendiente / seguimiento

- [x] Sincronización de notas con el vault **fuera** del repo: Pass espeja su carpeta
      de notas (`vault/subcarpeta`) ⇄ `notes/` del repo (`setNotesDir` +
      `seedVaultFromRepoNotes` unión aditiva inicial + `mirrorNotesToRepo`/
      `mirrorNotesFromRepo`), gobernado por git (conflictos = conservar ambas). Así
      cada dispositivo mantiene su propio vault de Obsidian y comparte solo las notas
      de Pass. `MainWindow` vigila esa carpeta y dispara `scheduleAutoPush` (debounce
      30 s). Test E2E `notesSyncFromExternalVault`.
- [ ] Validación **E2E real** contra un repo privado de GitHub de prueba (clonar desde
      *Ajustes*, crear sesión, verificar commit+push y la fusión en un 2º dispositivo).
- [ ] Revisión de seguridad dedicada de M8.3/M8.4 antes de publicar (checklist hecha;
      ver resumen de contingencia más abajo).
- [ ] En Ajustes Conexiones sincronizar ahora de GitHub debe ir debajo de última sincronización. //TODO human
