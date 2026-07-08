# Informe de auditoría de robustez y seguridad — PASS

- **Proyecto:** PASS — dashboard de escritorio C++20 / Qt 6 (Widgets), local-first.
- **Fecha:** 2026-06-13
- **Alcance:** todo el árbol `core/` y `app/` en `main` (124 ficheros `.cpp`/`.h`), `scripts/deploy.ps1`, `CMakeLists.txt`/`CMakePresets.json` y la documentación de seguridad (`docs/`).
- **Metodología:** auditoría multi-agente en 9 dominios de amenaza en paralelo, **cada hallazgo verificado de forma adversarial** (un revisor escéptico independiente intentó refutarlo leyendo el código real), más un crítico de completitud. Verificación manual adicional de los hallazgos de UI/concurrencia. Las severidades de este informe son las **posteriores a la verificación**, no las propuestas iniciales.

---

## 1. Resumen ejecutivo

**Postura general: buena.** PASS está escrito con conciencia de seguridad evidente (el "modo contingencia" documentado se nota en el código). Las defensas en los puntos que más importan están **correctamente implementadas y se verificaron como sólidas**:

- **OAuth2 + PKCE (S256)** bien hecho: parámetro `state` anti-CSRF, redirect atado a `127.0.0.1` con puerto efímero, endpoints HTTPS fijos, **sin** desactivar la verificación TLS, scope mínimo, refresh coalescido.
- **SQL 100 % parametrizado**: no se encontró una sola concatenación de valores no confiables; incluso los nombres de tabla dinámicos provienen de *whitelists* fijas. No hay SQLi explotable ni siquiera desde la entrada de sync remota.
- **Ejecución de `git`** como `programa + lista de argumentos` (nunca shell), *whitelist* de URL estricta, credenciales delegadas a Git Credential Manager, entorno endurecido y salida redactada.
- **Credenciales** en el Administrador de credenciales de Windows (DPAPI), nunca en disco, repo ni logs; `SecureZeroMemory` en los buffers del SDK.
- **Migraciones** atómicas e idempotentes; `foreign_keys` activo; **sin hilos**, por lo que no hay carreras sobre la única conexión SQLite.

El trabajo de remediación se concentra en **un único punto de palanca** y unas pocas mejoras transversales.

### Conteo de hallazgos (severidad tras verificación)

| Severidad | Nº | 
|---|---|
| 🔴 Alta | 1 |
| 🟠 Media | 2 |
| 🟡 Baja | 18 |
| ⚪ Informativa | 9 |
| ✅ Refutado (falso positivo) | 3 |

### Las 4 acciones de mayor impacto

1. **[ALTA] Validar/acotar los enteros en el deserializador de sync** (`SyncSerializer::fromJson`) y blindar el bucle de `StrategyCatalog::proposals`. Esta única frontera alimenta el **cuelgue de la UI por bucle infinito** (H-1), el desbordamiento de `int` en el timer, el *overshoot* al reanudar y el truncamiento en estadísticas. Es el arreglo con mejor relación coste/beneficio.
2. **[MEDIA] Sacar el pipeline de sync git del hilo de la GUI** (o trocearlo): hoy congela la app hasta 2 min ante un remoto lento u hostil (M-2).
3. **[MEDIA] Añadir flags de *hardening* de compilador/enlazador** (`-Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2`, ASLR/DEP en MinGW): defensa en profundidad barata sobre el código que parsea entrada semi-confiable (M-1).
4. **[BAJA] En la UI, usar `Qt::PlainText` / `toHtmlEscaped()`** para todo `QLabel`/`QMessageBox` que muestre datos derivados de sync, vault o respuestas de red (DashboardView, errores de git/Google, `device_name`).

---

## 2. Modelo de amenaza y fronteras de confianza

PASS es **local-first**: el atacante NO es "internet genérico". Las severidades se calibran según qué dato cruza una frontera de confianza:

1. **Contenido del repo git remoto** — otro dispositivo del usuario o el asistente móvil *PassPort* (escritor explícitamente **no confiable** según `docs/passport-integration.md`). JSON por registro + tombstones + notas `.md`. → **El importador debe tratarlo como hostil.** Frontera principal.
2. **Respuestas de la API de Google Calendar** (HTTPS): JSON, etags, tokens. Semi-confiable.
3. **El redirect OAuth2 en loopback** `127.0.0.1`.
4. **Ficheros del vault de Obsidian** (el watcher los relee; también llegan por sync).
5. **Entrada de UI** (diálogos): el propio usuario, mayormente confiable.

El usuario local es de confianza; el peso recae en las **fronteras 1 y 2**.

---

## 3. Hallazgos

### 🔴 ALTA

#### H-1 · Bucle infinito en `StrategyCatalog::proposals` con un descanso negativo de una estrategia sincronizada (cuelga la UI)
- **Tipo:** robustez (DoS local) · **CWE-835** · **Confirmado (alta).**
- **Ubicación:** `core/src/session/StrategyCatalog.cpp:27-35`; origen en `core/src/sync/SyncSerializer.cpp:140-148`.
- **Descripción:** `proposals()` itera `while (true)` añadiendo ciclos mientras `total + next <= targetMinutes`, con `next = breakAfter(s, cycles) + s.workMinutes`. No hay tope de ciclos ni exigencia de `next > 0`. El deserializador de estrategias de sync **solo comprueba que los campos sean numéricos** (`isDouble()`), sin acotar rango: un `break_min` (o `long_break_min`) suficientemente negativo hace `next <= 0`, `total` deja de crecer y el bucle no termina nunca.
- **Cadena verificada:** `SyncSerializer::fromJson` (sin clamp) → `SyncImporter::lwwUpsertStrategy` (persiste crudo) → `StrategyRepository` (relee crudo) → `SessionSetupDialog::refreshProposals()` (`SessionSetupDialog.cpp:115`, invocado al abrir "Nueva sesión" y en cada cambio del spinbox) y `StudyView.cpp:115`.
- **Impacto:** una estrategia personalizada maliciosa o corrupta proveniente del repo de sync (frontera 1 / PassPort) **congela el hilo de la GUI al 100 % de CPU** con solo abrir el diálogo de nueva sesión. No requiere más interacción.
- **Recomendación:**
  1. En `SyncSerializer::fromJson(PomodoroStrategy)`: clampar `break_min`/`long_break_min` a `[0, 600]`, `work_min` a `[1, 600]`, `cycles_before_long` a `[0, 100]` (o rechazar con `return false`).
  2. **Defensa en profundidad (más importante):** blindar el bucle de `proposals()` con un tope duro de ciclos y/o exigir `next > 0` para continuar, de modo que datos corruptos no cuelguen la UI aunque la validación de entrada fallase.
- **Nota de verificación:** el revisor confirmó que con `next <= 0` `total` no crece, por lo que **no hay** desbordamiento de `int` por esta vía; el cuelgue es por iteración infinita pura.

---

### 🟠 MEDIA

#### M-1 · Ausencia total de flags de *hardening* de compilador y enlazador
- **Tipo:** robustez / defensa en profundidad · **CWE-1327** · **Confirmado (media).**
- **Ubicación:** `CMakeLists.txt` (raíz) y los tres subdirectorios — ningún `add_compile_options`/`target_compile_options`/`add_link_options`.
- **Descripción:** el binario se compila con el nivel de warnings por defecto de GCC y sin ninguna mitigación. Faltan: `-Wall -Wextra -Wformat -Wformat-security`, `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2` (en Release) y, para MinGW, `-Wl,--dynamicbase` (ASLR), `-Wl,--nxcompat` (DEP), `-Wl,--high-entropy-va`. El binario parsea entrada semi-confiable (JSON de sync, JSON de Google, `.md` del vault).
- **Impacto:** un bug de memoria en los parsers (fronteras 1/2/4) pierde las capas de mitigación que convierten un crash en una explotación difícil. No explotable por sí mismo, pero degrada la defensa justo donde llegan datos hostiles.
- **Recomendación:** en el `CMakeLists.txt` raíz, condicionado a GNU/Clang: `add_compile_options(-Wall -Wextra -Wformat -Wformat-security -fstack-protector-strong)`, `-D_FORTIFY_SOURCE=2` en Release, y `add_link_options(-Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)`. Verificar con `llvm-readobj`/`dumpbin` que DEP/ASLR quedan marcados en `pass.exe`.

#### M-2 · El pipeline de sync git se ejecuta **bloqueante en el hilo de la GUI**
- **Tipo:** robustez (DoS por congelación) · **CWE-400** · **Verificado manualmente (media).** *(Gap detectado por el crítico de completitud; confirmado leyendo el código.)*
- **Ubicación:** `core/src/sync/GitSyncService.cpp` — `runCycle()` (`:236`) encadena ~25 llamadas `runBlocking` (`preflight`, `stageLocal`, `mergeAndImport`, `resolveConflicts`); `MainWindow.cpp:166-167` (`shutdownSync(8000)` en `closeEvent`).
- **Descripción:** **no existe ningún hilo de trabajo** (verificado: sin `QThread`/`QtConcurrent`/`std::thread`/`moveToThread`). El ciclo de sync se dispara por timer en el hilo de eventos de Qt y ejecuta `git merge`/`fetch` con `kNetTimeoutMs = 120 s` (`GitSyncService.cpp:26`), más la importación SQL completa (`importAll` parsea todos los `.json` del árbol en el primer ciclo).
- **Impacto:** un repo remoto lento u hostil (frontera 1) **congela la interfaz hasta 2 minutos por operación de red**. El cierre de la app bloquea hasta 8 s con `shutdownSync`. Se solapa con los hallazgos de "sin límite de nº de ficheros" (B-7) y "sin tope de páginas" (B-8): el síntoma aquí es congelación de UI, no solo memoria.
- **Recomendación:** mover `GitSyncService` a un `QThread`/worker (la clase ya usa señales, encaja bien), o trocear `merge`+`import` en pasos asíncronos con `GitRunner::run` (la variante asíncrona ya existe). Como mínimo, reducir `kNetTimeoutMs` y acotar el trabajo por ciclo.

---

### 🟡 BAJA

> Salvo indicación, todas son **disparables desde la frontera de sync (1)** o son robustez local; ninguna es ejecución de código ni escalada.

**Frontera de sync / deserialización**

- **B-1 · Enteros remotos sin validación de rango.** `SyncSerializer.cpp:140-148,238,253-254`. `work_min`/`break_min`/`planned_min`/`actual_sec`/`resume_*` se asignan con `toInt()` sin acotar; los enteros de `StudySession` ni siquiera exigen `isDouble()` (un string/bool se traga como `0`). Alimenta H-1 y B-2. Los consumidores aguas abajo (`qBound`/`qMax`, `WHERE actual_sec > 0`) mitigan parte, pero el dato corrupto entra a la BD. **Arreglo raíz junto con H-1.**
- **B-2 · Desbordamiento de `int` en `phasesFor`.** `SessionTimerService.cpp:14,20-21` y fallback `StudyView.cpp:120`: `workMinutes * 60` / `plannedMinutes * 60` se calcula en `int` antes de promover a `qint64`. Un valor enorme (cabe en `int`) desborda con signo (UB) → fases con segundos negativos, el timer salta fases al instante. *Arreglo:* `qint64(workMinutes) * 60` desde el inicio, además del clamp de B-1.
- **B-3 · `resume_phase_elapsed_sec` sin clamp superior al reanudar.** `SessionTimerService.cpp:45-46`: el índice se clampa con `qBound` pero el *elapsed* solo se protege contra negativos, no contra la duración de la fase. Un valor inflado por sync encadena `advancePhase` y salta varias fases al pulsar "Reanudar", corrompiendo el registro. *Arreglo:* `qBound<qint64>(0, elapsed*1000, faseSeconds*1000)`.
- **B-4 · Sin límite global de nº de ficheros/registros en `importAll`.** `SyncImporter.cpp:511-516`. Hay tope por fichero (256 KB) pero no sobre la cantidad: un repo con cientos de miles de JSON válidos se carga entero en memoria antes del commit. CWE-770. *Arreglo:* tope de ficheros/registros y/o commit por lotes.
- **B-5 · `exec()` sin comprobar en tombstones y resolución de colisiones.** `SyncImporter.cpp:340-361,403-413,443-445,134-139`. Las sentencias intermedias (anular FKs, borrar perdedor) ignoran el retorno; si una falla de forma transitoria pero las posteriores y el commit tienen éxito, el merge/tombstone se confirma **a medias**, perdiendo la atomicidad. *Arreglo:* propagar el `bool` de cada `exec()` para forzar `rollback`.
- **B-6 · Comparador de tombstone hace *fail-open* si `updated_at` local es inválido.** `SyncImporter.cpp:332-338`. Si la columna local no parsea, el guard anti-pérdida se omite y la lápida borra la fila aunque pudiera ser más nueva. Solo alcanzable con corrupción local fuera de banda (no por el peer remoto, que está validado), pero contradice el invariante documentado. *Arreglo:* conservar la fila si `!localUpdated.isValid()`.
- **B-7 · Pull de Google sin tope de páginas ni de tamaño de cuerpo.** `GoogleSyncService.cpp:102-105` encadena `fetchPage` mientras haya `nextPageToken` sin contador; `GoogleCalendarClient.cpp:114` hace `readAll()` sin límite. CWE-770. Riesgo: agotamiento de memoria si Google (tras TLS) devuelve datos anómalos. *Arreglo:* tope de páginas y límite de cuerpo.
- **B-8 · `applyPage` no comprueba ni revierte la transacción y avanza el `syncToken`.** `GoogleSyncService.cpp:109-131`. Ignora el retorno de `transaction()`/`commit()` y de cada upsert; si uno falla a mitad de página, se hace commit igual y el `syncToken` avanza en `finishPull`, de modo que esos eventos espejo **nunca se reintentan**. *Arreglo:* ante fallo, `rollback` y no avanzar el token (tratar como error de pull).
- **B-9 · Espejo de notas remoto sin límite de tamaño ni de cantidad.** `GitSyncService.cpp:55-103,149-168`. A diferencia de la rama JSON (256 KB), las notas `.md` se copian sin tope, y `sameContent` lee ambos ficheros completos en memoria; `copyOverwrite` usa `QFile::copy` (no atómico). DoS local de baja gravedad (git ya escribió esos bytes en el clon). *Arreglo:* tope por `.md`, comparación por hash en streaming, copia con `QSaveFile`.

**Capa SQL / datos**

- **B-10 · Lecturas que silencian errores de SQL.** `SessionRepository.cpp:75-84` y patrón repetido en Event/Subject/Strategy/Topic repos. Un `exec()` fallido se trata igual que "sin filas" (sin `qWarning`/`lastError`): una BD corrupta se presenta como "no tienes datos". *(El exporter sí comprueba sus `exec()`, así que no re-exporta un espejo vacío.)* *Arreglo:* registrar `q.lastError()` y distinguir vacío de error.
- **B-11 · Conexión SQLite queda abierta tras fallo de PRAGMA/migración.** `Database.cpp:17-30`. Si falla `PRAGMA foreign_keys` o `applyMigrations`, retorna con `m_ok=false` dejando la conexión viva (el destructor la cierra; no hay fuga real). Hoy seguro porque todos los `handle()` están guardados por `isOpen()`. *Arreglo:* `db.close()` en los caminos de fallo.

**Calendario / Google**

- **B-12 · `externalId` de Google concatenado en la URL sin percent-encoding.** `GoogleCalendarClient.cpp:152,174` (`patchEvent`/`deleteEvent`). Verificado empíricamente: un id con `?`/`/`/`..` altera el path o inyecta query en una petición PATCH/DELETE autenticada. **Acotado:** el host es fijo (`googleapis.com`), no hay redirección cross-host ni fuga de token; el peor caso es operar sobre otro recurso del **propio** calendario del usuario. La frontera de sync no alcanza esta vía (los eventos sincronizados llevan `provider='local'`). *Arreglo:* `QUrl::toPercentEncoding` del segmento o validar el alfabeto `[a-v0-9]` del id.
- **B-13 · Fechas malformadas de Google → cadena vacía → evento invisible.** `GoogleEventMapper.cpp:13-20` no valida `isValid()`; una fecha basura se persiste como `start_utc`/`end_utc` vacíos y el evento cae fuera de todo rango en `between()`. **Verificado que el disparador alegado (RFC3339 con fracción de segundo) NO ocurre** (Qt 6.11 lo parsea bien) y la API real siempre trae fecha con `singleEvents=true`; es defensa en profundidad. *Arreglo:* guard `isValid()` en el mapper y rechazar bounds vacíos en `EventRepository`.
- **B-14 · Round-trip de eventos *all-day* sensible al cambio de zona del sistema.** `GoogleEventMapper.cpp:13-15` vs `:65-67`: usa medianoche local convertida a UTC; un cambio de zona horaria del sistema entre guardado y re-subida desplaza la fecha ±1 día. *(El DST ordinario NO lo dispara.)* *Arreglo:* tratar la fecha civil como `QDate` puro / usar UTC de forma simétrica.

**Notas / ficheros**

- **B-15 · Inyección de YAML en el frontmatter por nombre de asignatura con `\n`/`:`.** `NoteSerializer.cpp:55-66` concatena `clave: valor` sin escapar; `VaultService::createNote` escribe `subject.trimmed()` (que no elimina saltos internos). Un nombre de asignatura llegado por sync sin sanear inyecta claves YAML o rompe el frontmatter. Confinado a los `.md` locales del usuario (sin RCE). *Arreglo:* normalizar a una línea y entrecomillar/escapar valores con `:` en `setValue`, o sanear el nombre al importarlo.
- **B-16 · `rewriteNotesSubject` usa `replace()` global no anclado.** `SubjectAdminService.cpp:192-199`. Al renombrar, `doc.body.replace("Asignatura: "+oldName, ...)` y `tags.replace(oldSlug, ...)` pueden alterar texto de los apuntes del usuario o corromper el token `pass` del frontmatter si el slug es corto (p. ej. `as`). *(El origen es UI local, no sync.)* *Arreglo:* parsear la lista de tags y reemplazar solo el elemento exacto; anclar la sustitución del cuerpo a la línea de plantilla.

**Build / git / UI**

- **B-17 · `git` se invoca por nombre desnudo (`"git"`) dependiente del PATH.** `GitRunner.h:69`, `GitRunner.cpp:140,148`. CWE-426. Resolución vía PATH heredado (en Windows la búsqueda incluye también el directorio actual del proceso lanzador). Requiere que el atacante ya controle el PATH del usuario → bajo. *Arreglo:* resolver una vez con `QStandardPaths::findExecutable("git")` y cachear la ruta absoluta (manteniendo `setGitProgram()` para tests).
- **B-18 · `device_name` de presencia sin tope de tamaño en un diálogo.** `GitSyncService.cpp:543-567` → `MainWindow.cpp:72-84`. *(Gap del crítico, verificado.)* `checkPresence()` lee `presence/*.json` del repo remoto y emite `device_name` sin validar longitud; se muestra en un `QMessageBox.arg(device)`. Un `device_name` de megabytes (frontera 1) infla diálogo y memoria; `presence/*.json` se parsea sin el `kMaxFileBytes` que sí tiene el importador. *Arreglo:* tope de tamaño al leer `presence/*.json` y truncar/sanear `device_name`.

---

### ⚪ INFORMATIVA (endurecimiento / no explotable)

- **I-1 · `WinCredTokenStore::write` no consulta `GetLastError()` y los llamantes ignoran su retorno.** `WinCredTokenStore.cpp:64-66`; `GoogleAuthService` `persistTokens` (`:179,184,188`). Un fallo al persistir el `refresh_token` deja la app aparentemente conectada pero desconectada tras reiniciar, sin diagnóstico. *Arreglo:* registrar `GetLastError()` (sin el secreto) y comprobar el retorno de `write()`.
- **I-2 · El secreto descifrado se materializa en un `QString` del heap no borrable.** `WinCredTokenStore.cpp:39-49` limpia el buffer del SDK pero devuelve el secreto por valor; queda en el heap sin `wipe`. Riesgo residual: volcados de memoria/swap. Aceptable en local-first; documentar o exponer un buffer wipeable.
- **I-3 · Sin validar el tamaño del blob antes de `CredWriteW`** (`CRED_MAX_CREDENTIAL_BLOB_SIZE` = 2560 B). `WinCredTokenStore.cpp:59-64`. Caso límite improbable (los tokens reales caben de sobra); sin over-read en lectura. *Arreglo:* comprobar el límite y fallar con mensaje claro.
- **I-4 · `manifest.format` no numérico se trata como compatible.** `SyncImporter.cpp:116-130`. Un manifest manipulado con `format` no entero esquiva el guard de versión (cae a v1). Marginal: cada fichero se valida aparte. *Arreglo:* si `format` está presente pero no es entero, abortar.
- **I-5 · `redacted()` solo cubre credenciales con forma `scheme://user@`.** `GitRunner.cpp:78-84`. Un token suelto (`ghp_…`) en stderr no se redactaría. Muy improbable (GCM + whitelist sin userinfo). *Arreglo:* añadir patrones de PAT de GitHub al redactor.
- **I-6 · Comandos git sin separador `--` para refs derivadas del remoto.** `GitSyncService.cpp` (`m_branch`). Sin inyección real (siempre va tras prefijo `origin/`/`HEAD:`), pero conviene validar `m_branch` y usar `--` por defensa en profundidad.
- **I-7 · Clone/fetch sin endurecer.** `GitSyncService.cpp:581-612,311-329`. No fija `protocol.allow`, `transfer.fsckObjects` ni `core.hooksPath=`. Mitigado porque la URL está en *whitelist* (solo `github.com`) y git no transfiere config/hooks del remoto. CWE-829. *Arreglo:* añadir esos `-c` por defensa en profundidad.
- **I-8 · `windeployqt` empaqueta drivers SQL no usados** (`qsqlmysql/odbc/psql/ibase`) en `dist/sqldrivers/`. PASS solo usa SQLite. Superficie/peso extra; plugins de carga perezosa nunca invocados. *Arreglo:* podar `dist/sqldrivers/` salvo `qsqlite.dll`.
- **I-9 · Errores de git/Google en `QLabel` `Qt::AutoText`.** *(Gap del crítico, verificado.)* `SettingsView.cpp:240,409` (`m_gitError` sin `setTextFormat`) y `DashboardView.cpp:55,113-118` (`m_lastNote` en `Qt::RichText` con el título de nota). En Windows los nombres de fichero prohíben `<>:`, lo que limita la inyección de HTML, pero es un patrón frágil (entidades `&`, o si el título pasara a leerse del frontmatter). *Arreglo:* `Qt::PlainText` o `toHtmlEscaped()` en esos labels y en los `QMessageBox` que muestran texto de red.

> **Robustez adicional (menor):** el watcher del vault (`VaultWatcher.cpp:52-64`) reañade todos los `.md` en cada cambio de directorio sin tope, lo que puede agotar handles de `QFileSystemWatcher` con un `notes/` de miles de ficheros tras sync; conviene vigilar el directorio en vez de fichero a fichero y manejar el retorno de `addPaths`. El truncamiento `qint64→int` en `StatsService.cpp:49` (`actual_sec/60`) se solapa con B-1.

---

### ✅ Refutados (revisados y descartados como falsos positivos)

La verificación adversarial **descartó** tres hallazgos propuestos; se listan por transparencia:

- **Path traversal al espejar notas `.md`.** *Refutado.* `rel` se calcula con `QFileInfo::absoluteFilePath().mid(...)` sobre ficheros que existen realmente bajo `base`; Qt ya normaliza `.`/`..`, así que el destino nunca escapa del vault. Arista residual real (no la alegada): en POSIX `QDir::Files` sigue symlinks, lo que **copiaría contenido externo hacia** el vault (no fuera de él); en Windows (plataforma objetivo) git no crea symlinks. Mejora opcional: `isSafeRelPath()` + `QDir::NoSymLinks`.
- **`CRED_PERSIST_LOCAL_MACHINE` "no es per-usuario" (×2).** *Refutado.* Premisa técnica incorrecta: en credenciales `CRED_TYPE_GENERIC` el almacén ya es per-usuario; `LOCAL_MACHINE` significa "persiste entre sesiones del mismo usuario, sin roaming". El comentario del código es correcto.

---

## 4. Fortalezas verificadas (qué está bien hecho)

- **OAuth2/PKCE:** `setPkceMethod(S256)`, `state` anti-CSRF gestionado por Qt, loopback `127.0.0.1` con puerto efímero cerrado al terminar, endpoints HTTPS fijos, **sin** `ignoreSslErrors`/`VerifyNone`, scope mínimo `calendar.events`, manejo correcto de 401/403/410/`invalid_grant`, refresh coalescido (`m_refreshing` + `m_pending`).
- **SQL:** todo `prepare`/`addBindValue`; UUIDs normalizados con `QUuid::fromString` antes de tocar la BD; nombres de tabla dinámicos desde *whitelist* (`QHash`); listas de columnas constantes. **Sin SQLi** ni desde sync.
- **git:** `QProcess::start(programa, QStringList)` sin shell; `isAllowedRemoteUrl` estricta (solo `github.com`, sin userinfo/puerto/query/fragment); `GIT_TERMINAL_PROMPT=0`, `GIT_PAGER=cat`; timeouts con `kill`; credenciales solo en GCM.
- **Credenciales:** Administrador de credenciales de Windows + DPAPI; `SecureZeroMemory` en los buffers del SDK; nada en QSettings/ficheros/repo/logs; el diálogo usa modo `Password` y no precarga el secret.
- **Migraciones:** atómicas (bump de `user_version` en el mismo commit) e idempotentes; `PRAGMA foreign_keys` por conexión; `defer_foreign_keys` donde se reasignan FKs.
- **Concurrencia:** **sin hilos** → no hay carreras sobre la única conexión `QSqlDatabase`. (El reverso es M-2: todo corre en el hilo GUI.)
- **Sync:** modelo *un JSON por registro* + LWW + tombstones explícitos (nunca "ausencia = borrado"); guard de `manifest.format`; check `id == nombre_de_fichero`; red de seguridad de FKs colgantes al final del import.
- **Notas/ficheros:** escrituras atómicas con `QSaveFile`; `sanitizeTitle`; borrado en cascada solo sobre ficheros listados por el propio vault.
- **Supply chain:** `.gitignore` excluye `build/`, `dist/`, `CMakeUserPresets.json`, `*.user`; **sin** secretos/tokens/BD versionados; `deploy.ps1` copia DLLs solo del directorio fijo de MSYS2 y usa `objdump` con `programa+args` (sin shell); la documentación de seguridad es honesta y no filtra credenciales de ejemplo.

---

## 5. Plan de remediación priorizado

| # | Acción | Severidad | Esfuerzo | Ficheros |
|---|--------|-----------|----------|----------|
| 1 | Clampar enteros en `SyncSerializer::fromJson` + tope/`next>0` en `proposals()` | 🔴 H-1, 🟡 B-1/B-2/B-3 | Bajo | `SyncSerializer.cpp`, `StrategyCatalog.cpp`, `SessionTimerService.cpp` |
| 2 | Sacar `GitSyncService` del hilo GUI (o trocear async) | 🟠 M-2 | Medio-alto | `GitSyncService.cpp`, `MainWindow.cpp` |
| 3 | Flags de *hardening* en CMake | 🟠 M-1 | Bajo | `CMakeLists.txt` |
| 4 | `Qt::PlainText`/`toHtmlEscaped()` en labels/diálogos con datos externos; tope a `device_name` | 🟡 B-18, ⚪ I-9 | Bajo | `DashboardView.cpp`, `SettingsView.cpp`, `MainWindow.cpp`, `GitSyncService.cpp` |
| 5 | Atomicidad de import/pull: comprobar `exec()`/transacciones, no avanzar token ante fallo | 🟡 B-5/B-8 | Bajo | `SyncImporter.cpp`, `GoogleSyncService.cpp` |
| 6 | Límites de DoS: nº ficheros en `importAll`, páginas/cuerpo en pull, tamaño de notas/presence | 🟡 B-4/B-7/B-9/B-18 | Bajo | `SyncImporter.cpp`, `GoogleSyncService.cpp`, `GitSyncService.cpp` |
| 7 | Endurecer parsing: percent-encode `externalId`, `isValid()` en fechas, escapar YAML, anclar `rewriteNotesSubject` | 🟡 B-12/B-13/B-15/B-16 | Bajo-medio | `GoogleCalendarClient.cpp`, `GoogleEventMapper.cpp`, `NoteSerializer.cpp`, `SubjectAdminService.cpp` |
| 8 | Higiene: resolver `git` a ruta absoluta, log de errores en lecturas SQL/`CredWrite`, podar drivers SQL, `-c` de git | 🟡 B-10/B-17, ⚪ I-1/I-7/I-8 | Bajo | varios |

---

## 6. Apéndice — cobertura

**Dominios auditados (9):** OAuth2/PKCE · almacenamiento de credenciales (WinCred) · ejecución de git (QProcess) · deserialización/entrada remota (importador de sync) · capa SQL/SQLite/migraciones · ficheros del vault/notas/cascada de asignaturas · mapeo Google↔local/write-through · robustez de sesiones/timer/planificador · build/dependencias/despliegue.

**Capa UI (`app/`)** y **concurrencia hilo-GUI**: cubiertas vía el crítico de completitud + verificación manual (M-2, B-18, I-9).

**Limitaciones:** la integración con Google y la sync por GitHub requieren validación E2E con cuentas reales (fuera del alcance estático). Este informe es análisis de código; no sustituye pruebas dinámicas ni fuzzing de los parsers (recomendable sobre `SyncImporter`/`GoogleEventMapper` una vez aplicado el punto 1).
