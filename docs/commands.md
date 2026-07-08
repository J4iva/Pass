# CLI por comando — escribir en Pass desde el repo de sync

Pass ejecuta **comandos en texto** escritos en la carpeta `command/` del repo de
sincronización. Se procesan automáticamente al hacer un *pull* y al arrancar la
app, **sin abrir Pass ni tener acceso directo** al equipo. Es una capa de azúcar
sobre el formato del repo (ver `docs/passport-integration.md`): el *writer* no
necesita generar JSON ni UUID a mano, solo escribir una línea tipo CLI.

```
[tú, desde cualquier sitio]  escribes command/algo.passcmd  →  commit + push
                                      │
                              GitHub (repo privado)
                                      │
[Pass escritorio]  pull → procesa el comando → aplica el efecto → mueve a processed/
```

> MVP: **solo `create`** (nunca borrado). Editar queda reservado para el futuro.

---

## 1. Dónde y cómo se escribe

- **Ubicación**: `<repo>/command/<nombre>.passcmd` (un fichero = un comando).
- **Contenido**: una sola línea con la sintaxis de §2. Se ignoran los ficheros que
  no terminen en `.passcmd`, y no se entra en subcarpetas (salvo `processed/`).
- **Post-procesado**: al aplicarse con éxito, el fichero se **mueve a
  `command/processed/`** en el mismo commit/push, de modo que todos los
  dispositivos lo ven como "ya hecho" y no lo reprocesan.
- **Errores**: un comando mal formado o semánticamente inválido **se queda en
  sitio** (no se mueve) para que lo revises; **no** rompe el ciclo de sync.
- **Límites** (seguridad): se ignora cualquier `.passcmd` mayor de **8 KiB**, y se
  procesan como máximo **200** por ciclo de sincronización.

---

## 2. Gramática

```
Pass create <entidad> [posicionales] [--flag valor | --flag=valor | --bool-flag]*

entidad ∈ {subject, topic, note, event, task, session}
```

Tokenización tipo shell: las comillas dobles agrupan un valor con espacios; dentro,
`\"` y `\\` se interpretan como `"` y `\`. Sin expansión de variables ni operadores.

**Fechas**: ISO-8601. Con sufijo `Z` = UTC (recomendado). Sin él se interpreta como
hora local del dispositivo que procesa (limitación a tener en cuenta si escribes
desde otra zona).

### 2.1 `create subject` — asignatura nueva

```
Pass create subject <nombre> [--color "#rrggbb"]
```

```
Pass create subject Cálculo --color "#3478f6"
```

Si ya existe una asignatura con ese nombre, el comando se da por bueno y **no**
crea un duplicado (reutiliza la existente).

### 2.2 `create topic` — tema de una asignatura

```
Pass create topic <nombre> --subject <asignatura>
```

```
Pass create topic Integrales --subject Cálculo
```

`--subject` es **obligatorio** y debe existir (créala antes, en el mismo push).

### 2.3 `create note` — nota en el vault

```
Pass create note [<tema>] [--subject <asignatura>] [--body "..."]
```

```
Pass create note "Integrales por partes" --subject Cálculo --body "repaso de la regla..."
```

- Crea un `.md` en `notes/` del repo con el frontmatter y la plantilla de Pass
  (Apuntes/Dudas si lleva asignatura); Pass lo espeja a tu vault de Obsidian.
- Si indicas `--subject` debe existir; sin ella, se crea una nota libre.
- El nombre del fichero lleva fecha y hora, así que no pisa notas existentes.

### 2.4 `create event` — evento de calendario

```
Pass create event <título> --start <ISO> [--end <ISO>] [--all-day] [--subject <a>] [--desc "..."]
```

```
Pass create event Examen --start 2026-06-20T08:00:00Z --end 2026-06-20T10:00:00Z --subject Cálculo
```

Sin `--end`: si es `--all-day`, `end = start`; si no, `end = start + 1h`.

### 2.5 `create task` — tarea / deadline

```
Pass create task <título> --due <ISO> --subject <asignatura> [--desc "..."]
```

```
Pass create task "Entregar práctica 3" --due 2026-06-20T22:00:00Z --subject Cálculo
```

Una tarea es un evento especial (visible con prefijo `[T]`); `--due` es la fecha
de entrega y `--subject` es **obligatoria**.

### 2.6 `create session` — sesión de estudio planificada

```
Pass create session --start <ISO> [--minutes <int>] [--subject <a>] [--topic "..."] [--strategy <uuid>]
```

```
Pass create session --start 2026-06-14T16:00:00Z --minutes 50 --subject Cálculo --topic Integrales
```

Crea **dos** recursos enlazados: un evento de calendario ("Sesión: …") y la sesión
planificada (estado `planned`, 0 segundos trabajados). `--minutes` por defecto 50.
`--strategy` es opcional y rara vez necesario (déjalo fuera = sesión libre).

---

## 3. Idempotencia

El **id** de cada recurso se deriva del texto del comando (UUIDv5). Procesar el
mismo `.passcmd` dos veces —aquí o en otro dispositivo— produce el mismo id, así
que **no se duplica**: el segundo se resuelve como *omitido*. Las notas llevan
además una marca `pass_command_id` en su frontmatter con el mismo propósito.

> Si reescribes el fichero con un texto distinto, se considera otro comando y
> creará un recurso nuevo (con un id distinto).

---

## 4. Desactivar la feature

Ajustes → opción del CLI por comando, o la clave `commands/enabled` (bool, por
defecto `true`). Si la desactivas, los `.passcmd` se quedan en `command/` sin
tocarse hasta que la vuelvas a activar.

---

## 5. Seguridad

El repo de sync es **privado** y es el perímetro de confianza: cualquiera con
acceso de escritura al repo puede crear datos en tu Pass mediante comandos. Esto
es lo mismo que ya ocurre con el formato JSON directo (`passport-integration.md`)
o con PassPort. Medidas adicionales propias del CLI:

- Parser de **lista blanca**: acción, entidad y flags se validan contra conjuntos
  fijos; cualquier token desconocido se rechaza. No hay shell ni `eval`.
- Límites de tamaño (8 KiB) y de número (200) por ciclo.
- Los nombres de nota se sanean (caracteres inválidos eliminados, longitud límite).
- Solo `create`: el CLI **nunca** borra ni modifica registros ajenos.
- Un comando que falla no aborta la sincronización ni corrompe la base de datos.
