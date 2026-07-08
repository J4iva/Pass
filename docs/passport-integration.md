# PASS ↔ PassPort — Especificación de integración

> Documento de handoff para construir **PassPort** (asistente móvil/portátil) de
> forma que escriba datos en **PASS** (app de escritorio) **sin** abrir ningún
> servicio en el escritorio. Está escrito para que un agente (Claude Code) en el
> repo de PassPort pueda implementar el conector leyendo solo este `.md`.
>
> Fuente de verdad del formato: `core/src/sync/` de PASS (SyncSerializer,
> SyncExporter, SyncImporter) y `core/src/notes/`. Si PASS cambia el formato,
> **actualiza este documento**.

---

## 1. Qué es PASS

Dashboard de escritorio para estudiantes (C++/Qt6, SQLite local, Windows/MSYS2).
Gestiona:

- **Asignaturas** (`subjects`) y sus **temas** (`topics`).
- **Eventos de calendario** (`events`); una **tarea** es un evento con un prefijo
  especial en el título (ver §6.2).
- **Sesiones de estudio** pomodoro (`sessions`), planificadas o realizadas.
- **Estrategias** pomodoro (`strategies`).
- **Notas** en Markdown que viven como ficheros `.md` en un vault de Obsidian.

PASS ya sincroniza todo esto con un **repositorio privado de GitHub** (LWW por
registro + tombstones). PassPort se aprovecha de ese mismo canal.

---

## 2. Modelo de integración: el repo de sync es el buzón

**PassPort no llama a PASS.** PASS puede estar apagado o detrás de NAT. En su lugar:

```
[Móvil: texto/audio] → PassPort (backend always-on)
   1. transcribe (STT) y entiende la intención (LLM)
   2. confirma con el usuario
   3. clona/pull del repo privado, ESCRIBE ficheros en el formato de §5–§6
   4. commit + push
                          │
                    GitHub (repo privado)
                          │
[PASS escritorio]  ← hace pull en su sync normal e importa los cambios
```

Ventajas: cero superficie de ataque nueva en el escritorio (PASS sigue siendo
*pull-only* por git), funciona asíncrono, y reutiliza el validador de import que
PASS ya tiene endurecido.

**PassPort es un escritor no confiable** dirigido por un LLM y por un canal externo.
Todo lo de §8 (seguridad) es obligatorio.

---

## 3. Layout del repositorio

```
<repo>/
├── manifest.json              # { "format": 1 }  — guard de versión
├── data/
│   ├── subjects/<uuid>.json
│   ├── topics/<uuid>.json
│   ├── strategies/<uuid>.json    # solo personalizadas; PassPort normalmente NO escribe aquí
│   ├── events/<uuid>.json
│   ├── sessions/<uuid>.json
│   └── tombstones/<uuid>.json    # borrados explícitos (PassPort casi nunca)
└── notes/
    └── *.md                      # espejo de las notas de PASS (frontmatter incluido)
```

Reglas transversales (las aplica/espera el importador de PASS):

- **`manifest.json`**: si existe, `format` no puede ser mayor que el que soporta
  PASS (hoy `1`). PassPort debe escribir `{"format": 1}` si el repo no lo tiene, y
  **nunca** subirlo a un número mayor.
- **Nombre de fichero = id**: cada `<uuid>.json` debe contener un campo `"id"`
  cuyo UUID, **en minúsculas y sin llaves**, coincide *exactamente* con el nombre
  del fichero (sin `.md`/`.json`). Si no casa, PASS **ignora el fichero en
  silencio** (no falla, pero tu dato no entra). Formato UUID:
  `1b9d6bcd-bbfd-4b2d-9b5d-ab8dfbbd4bed` (8-4-4-4-12, hex minúscula).
- **Fechas**: ISO 8601 **en UTC**. Formato exacto que PASS emite/espera:
  `2026-06-13T17:30:00Z` (también acepta variantes ISO válidas, pero usa ese).
  Convierte SIEMPRE la hora local del usuario a UTC antes de escribir.
- **`updated_at` es el reloj de LWW**: en cada registro de `data/` es
  **obligatorio**. PASS solo aplica tu cambio si tu `updated_at` es **mayor** que
  el que ya tiene (`excluded.updated_at > tabla.updated_at`). PassPort debe poner el
  instante actual en UTC. Para *editar* un registro existente, vuelve a poner
  "ahora" (será mayor y ganará).
- **JSON**: objeto UTF-8 válido. PASS lo escribe *pretty/indentado*; PassPort puede
  hacerlo igual para diffs limpios, pero cualquier JSON válido vale.
- **Tamaño**: PASS ignora ficheros `> 256 KiB`. Mantén las notas y descripciones
  por debajo.
- **No toques lo que no es tuyo**: no reescribas ficheros de otros registros, no
  borres ficheros ajenos, no metas eventos con `provider != local` ni con
  `external_id` (eso es espejo de Google, gestionado por PASS).

---

## 4. Cómo referenciar asignaturas y temas (¡leer antes de escribir!)

Las tareas, eventos y sesiones referencian asignaturas por **`subject_id`** (UUID),
no por nombre. Antes de crear nada, PassPort debe **leer `data/subjects/`** (y
`data/topics/`) del repo para resolver el `subject_id` correcto a partir del nombre
que dijo el usuario ("Cálculo", "Historia"…).

- **Si la asignatura existe** → reutiliza su `id`.
- **Si no existe** → PassPort puede crearla escribiendo su `data/subjects/<uuid>.json`
  (§5.1) con un UUID nuevo, y referenciarla. PASS deduplica por nombre de forma
  segura: si dos dispositivos crean "Cálculo" con UUIDs distintos, PASS fusiona
  ambos (gana el UUID lexicográficamente menor y remapea las referencias). Por
  eso **crear-por-nombre es seguro**, pero reutilizar el id existente evita el
  baile de fusión.
- Las **notas** referencian la asignatura por **nombre** (texto), no por id (§6.4).

> Nota de robustez: si escribes un `subject_id` que no existe en el repo, al
> importar PASS pondrá ese campo a NULL (red de seguridad anti-huérfanos). Así que
> **crea la asignatura en el mismo push** que la tarea/sesión que la referencia.

---

## 5. Esquemas exactos de `data/`

Tipos: `string` (JSON string), `uuid` (string con UUID minúscula sin llaves),
`datetime` (string ISO-8601 UTC), `bool`, `int`. "opcional" = puede omitirse o ir
como `""`/`null`.

### 5.1 subjects/&lt;id&gt;.json

```json
{
  "id": "a1b2c3d4-...-...",
  "name": "Cálculo",
  "color": "#3478f6",
  "archived": false,
  "updated_at": "2026-06-13T17:30:00Z"
}
```

| campo | tipo | req. | notas |
|---|---|---|---|
| `id` | uuid | sí | = nombre de fichero |
| `name` | string | sí | **no vacío**; UNIQUE por nombre en PASS |
| `color` | string | no | hex `#rrggbb`; vacío permitido |
| `archived` | bool | no | por defecto `false` |
| `updated_at` | datetime | sí | reloj LWW |

### 5.2 topics/&lt;id&gt;.json

```json
{
  "id": "...",
  "subject_id": "a1b2c3d4-...",
  "name": "Integrales",
  "updated_at": "2026-06-13T17:30:00Z"
}
```

`subject_id` es **obligatorio** y debe apuntar a una asignatura (créala en el mismo
push si hace falta). UNIQUE por `(subject_id, name)`. Un tema huérfano (sin
asignatura) se descarta al importar.

### 5.3 events/&lt;id&gt;.json

```json
{
  "id": "...",
  "title": "Examen parcial",
  "description": "Aula 2.1",
  "start_utc": "2026-06-20T08:00:00Z",
  "end_utc":   "2026-06-20T10:00:00Z",
  "all_day": false,
  "rrule": "",
  "subject_id": "a1b2c3d4-...",
  "source_session_id": null,
  "updated_at": "2026-06-13T17:30:00Z"
}
```

| campo | tipo | req. | notas |
|---|---|---|---|
| `id` | uuid | sí | = nombre de fichero |
| `title` | string | sí | puede ir vacío, pero ponlo |
| `description` | string | no | |
| `start_utc` | datetime | sí | inicio (UTC) |
| `end_utc` | datetime | sí | fin (UTC); para tareas, = `start_utc` o +1h |
| `all_day` | bool | no | `false` por defecto |
| `rrule` | string | no | recurrencia; déjala `""` |
| `subject_id` | uuid | no | omite el campo si no aplica |
| `source_session_id` | uuid | no | solo si el evento lo generó una sesión (§6.3) |
| `updated_at` | datetime | sí | reloj LWW |

> No incluyas `provider`, `external_id` ni `etag`: PASS fuerza `provider='local'`
> al importar.

### 5.4 sessions/&lt;id&gt;.json

```json
{
  "id": "...",
  "subject_id": "a1b2c3d4-...",
  "strategy_id": null,
  "topic": "Integrales por partes",
  "planned_min": 50,
  "actual_sec": 0,
  "started_at": "2026-06-14T16:00:00Z",
  "ended_at": "",
  "status": "planned",
  "event_id": "<id del evento de planificación>",
  "updated_at": "2026-06-13T17:30:00Z"
}
```

| campo | tipo | req. | notas |
|---|---|---|---|
| `id` | uuid | sí | = nombre de fichero |
| `subject_id` | uuid | no | omite/`null` si libre |
| `strategy_id` | uuid | no | `null` = sesión libre (sin estrategia). Ver §6.3 |
| `topic` | string | no | texto libre |
| `planned_min` | int | no | minutos planificados (default 0) |
| `actual_sec` | int | no | segundos reales; **0** para una sesión planificada |
| `started_at` | datetime | no | para una sesión *planificada*, su fecha/hora de inicio prevista |
| `ended_at` | datetime | no | vacío en planificadas |
| `status` | string | sí | uno de: `planned`, `completed`, `aborted` |
| `event_id` | uuid | no | enlaza con un evento **local** (§6.3) |
| `updated_at` | datetime | sí | reloj LWW |

PassPort creará sesiones con `status: "planned"`. (`resume_phase_index` /
`resume_phase_elapsed_sec` son campos de reanudación interna de PASS: **no los
escribas**.)

Vínculo a evento remoto (Google): en vez de `event_id`, PASS usa un objeto
`"event": { "provider": "...", "external_id": "..." }`. **PassPort no usa esto**; crea
siempre eventos locales y enlaza por `event_id`.

### 5.5 tombstones/&lt;id&gt;.json (borrados — opcional)

```json
{ "entity": "events", "id": "<id-borrado>", "deleted_at": "2026-06-13T17:30:00Z" }
```

`entity` ∈ `subjects|topics|strategies|sessions|events`. PASS solo borra si
`deleted_at` > `updated_at` local del registro. PassPort rara vez necesita borrar; si
lo hace, escribe la tombstone (no borres solo el `.json`: "fichero ausente" **no**
significa borrado).

---

## 6. Recetas: las cosas que PassPort crea

### 6.1 Una nota de clase (caso principal)

Las notas **no** van en `data/`. Son ficheros `.md` en **`notes/`** del repo, con
frontmatter. PASS los espeja a su vault de Obsidian.

- **Nombre de fichero** (réplica de cómo lo hace PASS):
  `YYYY-MM-DD HHmm - <Asignatura> - <Tema>.md`
  Ej.: `2026-06-13 1730 - Cálculo - Integrales por partes.md`.
  - Si no hay asignatura: `YYYY-MM-DD HHmm - <Tema>.md`.
  - **Sanea** el título/tema/asignatura: elimina los caracteres
    ``\ / : * ? " < > | # ^ [ ]``, colapsa espacios, y trunca a 80 chars.
  - Si el nombre ya existe en el repo, añade ` (2)`, ` (3)`, …
- **Frontmatter + cuerpo** (formato que escribe PASS para una nota de estudio):

```markdown
---
created: 2026-06-13T17:30:00
app: pass
subject: Cálculo
tags: [pass, cálculo]
---

# Integrales por partes

> Asignatura: Cálculo · Creada: 13/06/2026 17:30

## Apuntes

<aquí el resumen generado por el LLM a partir del audio/texto>

## Dudas

```

- `subject:` es el **nombre** de la asignatura (string), y el tag es el nombre en
  minúsculas con espacios→guiones, dentro de `[pass, <tag>]`. Si no hay
  asignatura, frontmatter mínimo: `tags: [pass]` y cuerpo sin la línea de
  asignatura.
- El espejo de notas es **aditivo** y, ante conflicto, **conserva ambos** ficheros;
  por eso un nombre único (lleva fecha+hora) evita pisar nada.

### 6.2 Una tarea (entrega/deadline)

Una tarea es un **evento** (§5.3) cuyo **`title` empieza por `[T] `** (corchete-T-espacio).
Convención de PASS:

```json
{
  "id": "...",
  "title": "[T] Entregar práctica 3",
  "description": "...",
  "start_utc": "2026-06-20T22:00:00Z",
  "end_utc":   "2026-06-20T22:00:00Z",
  "all_day": false,
  "rrule": "",
  "subject_id": "a1b2c3d4-...",
  "updated_at": "2026-06-13T17:30:00Z"
}
```

- El **`start_utc` es la fecha de entrega**. `end_utc` = igual (o +1h).
- Una tarea **debe** llevar `subject_id` (asegúrate de que la asignatura existe).
- El prefijo `[T] ` se respeta tal cual (también es visible en Google Calendar).

### 6.3 Una sesión de estudio planificada

Para que la sesión aparezca **también en el calendario** (como hace PASS al
planificar), crea **dos ficheros en el mismo push**, enlazados:

1. Un **evento** local (§5.3) con:
   - `title`: p. ej. `"Sesión: Cálculo"`.
   - `start_utc`/`end_utc` = inicio y fin previstos (fin = inicio + `planned_min`).
   - `subject_id` de la asignatura.
   - `source_session_id` = el `id` de la sesión del paso 2.
2. Una **sesión** (§5.4) con:
   - `status: "planned"`, `planned_min`, `actual_sec: 0`.
   - `started_at` = inicio previsto.
   - `subject_id` y (opcional) `strategy_id`.
   - `event_id` = el `id` del evento del paso 1.

Si solo quieres registrar la sesión sin evento de calendario, escribe solo la
sesión y omite `event_id`.

**`strategy_id`**: déjalo `null` (sesión libre) salvo que el usuario tenga una
estrategia concreta. Las estrategias *builtin* de PASS usan UUIDs deterministas
que PassPort no conoce; no las referencies a ciegas. Crear estrategias nuevas desde
PassPort no es un caso esperado.

### 6.4 Un evento normal

Igual que §5.3, sin el prefijo `[T]`. `subject_id` opcional.

---

## 7. Flujo git de PassPort (orden y concurrencia)

Por cada acción confirmada:

1. `git clone` (primera vez) o `git pull --ff-only` (o fetch+merge) para tener la
   última versión. **Resuelve `subject_id` leyendo el repo recién traído.**
2. Escribe/actualiza los ficheros (§5–§6). Para *editar*, reescribe el fichero
   completo con `updated_at` = ahora.
3. `git add -A && git commit -m "PassPort: <resumen>"`.
4. `git push`. Si el push es rechazado (otro dispositivo empujó), `pull`+reintenta.
   Como el modelo es **LWW por registro + un fichero por registro**, los merges
   de git rara vez chocan; si chocan, vuelve a aplicar tu cambio con `updated_at`
   fresco.

El usuario de PASS verá los cambios en su próximo sync (manual o automático).

---

## 8. Seguridad (modo contingencia — obligatorio)

PassPort introduce entrada externa en un almacén que PASS importa **automáticamente**.

1. **Autoriza el canal**: el bot (Telegram/WhatsApp) debe responder **solo** a los
   `chat_id`/números en una allowlist tuya. Un bot abierto = cualquiera escribe en
   tu PASS.
2. **El LLM no escribe ficheros directamente**: el LLM produce **salida
   estructurada validada contra esquema** (intención + campos), y el código de
   PassPort construye el JSON/`.md`. Nunca dejes que el modelo genere rutas, nombres
   de fichero crudos ni el UUID a mano: genéralos tú.
3. **Confirmación humana** antes de cada `commit` de escritura ("He creado la
   tarea X para el viernes 20, ¿correcto?").
4. **Valida en PassPort** todo lo de §3 antes de escribir: UUID bien formado y = nombre
   de fichero, fechas UTC ISO, `updated_at` presente, tamaño < 256 KiB, sanea
   nombres de nota. (PASS revalida e ignora lo malformado, pero no dependas solo
   de eso.)
5. **Secretos** (token del bot, API keys del LLM/STT, credenciales del repo git)
   viven **solo en el backend de PassPort**, nunca en el cliente móvil ni en el repo.
   Usa el gestor de credenciales del sistema del servidor o variables de entorno
   fuera de control de versiones.
6. **Audios** como ficheros no confiables: límite de tamaño/duración, valida el
   tipo, bórralos tras transcribir.

---

## 9. Checklist de verificación E2E (F1: solo crear una tarea)

1. PassPort clona el repo, lee `data/subjects/`, resuelve/crea "Cálculo".
2. Escribe `data/events/<uuid>.json` con `title` `[T] ...`, `subject_id`,
   `start_utc` = deadline, `updated_at` = ahora. (Y el `subject.json` si era nueva.)
3. Commit + push.
4. En PASS: hacer sync → la tarea aparece en Calendario con su asignatura y, sin el
   prefijo, en la vista de tareas.
5. Repetir editando la tarea (mismo `id`, `updated_at` más nuevo) → PASS la
   actualiza (LWW).

---

## 10. Resumen de "qué escribe PassPort"

| Quiere crear… | Escribe |
|---|---|
| Nota de clase | `notes/<fecha> - <Asig> - <Tema>.md` con frontmatter (§6.1) |
| Tarea / deadline | `data/events/<id>.json` con `title` `[T] …` + `subject_id` (§6.2) |
| Evento de calendario | `data/events/<id>.json` (§6.4) |
| Sesión planificada | `data/sessions/<id>.json` (+ `data/events/<id>.json` enlazado) (§6.3) |
| Asignatura nueva | `data/subjects/<id>.json` (§5.1) — en el mismo push |
| Tema nuevo | `data/topics/<id>.json` (§5.2) |

Siempre: UUID minúscula = nombre de fichero · fechas UTC ISO-8601 ·
`updated_at` = ahora · `manifest.json` con `format: 1`.
