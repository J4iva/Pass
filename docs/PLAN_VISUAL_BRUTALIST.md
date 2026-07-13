# Pass · Plan visual — Brutalist ASCII + Nothing Dots

> Rediseño de la capa visual de la app de escritorio Pass (C++/Qt6, Windows/MSYS2).
> Estética: brutalismo ASCII con lenguaje de **dots** inspirado en Nothing.
> Skills de referencia: `industrial-brutalist-ui` + `design-taste-frontend`.

---

## 0. Design Read

> *Herramienta desktop de productividad/estudio (C++/Qt6) para sesiones largas diarias, lenguaje brutalista ASCII + dot-matrix Nothing, sustrato Dark Tactical/CRT, monocromo + un único rojo TrackPoint. QSS nativo + fuentes empaquetadas + iconografía de puntos en QPainter. Sin framework web, sin design-system de terceros.*

**Diales (taste-skill):**
- `DESIGN_VARIANCE 4` — rejilla blueprint rígida (el brutalismo es orden, no caos).
- `MOTION_INTENSITY 2-3` — app de escritorio, motion reducido por defecto (tick del timer, pulso de sync).
- `VISUAL_DENSITY 7-8` — cockpit: datos densos, separadores 1px, números en mono.

---

## 1. Decisiones bloqueadas

| Decisión | Valor |
|---|---|
| Sustrato | **Dark Tactical / CRT Terminal** (off-black, fósforo) |
| Política de color | **Monocromo + un único rojo TrackPoint** (categorías por patrón de dot, no por color) |
| Tipografía dot-matrix | **Fuente `.ttf` empaquetada** (Doto, con fallback) |
| Alcance | **Por fases**: shell+tema primero, luego vistas, luego widgets custom |

---

## 2. Auditoría del estado actual (puntos de entrada)

La UI de Pass es 100 % C++ imperativo. No existe ni un solo `.qss`, `.qrc`, `.ui`, ni fuente/icono empaquetado. Los estilos son ~8 `setStyleSheet()` inline dispersos + el tema nativo de Windows.

**Puntos de entrada de alto leverage:**
1. `app/main.cpp` — arranque único. Aquí se inyecta el QSS global, `QStyle("Fusion")`, carga de fuentes y paleta. Hoy no hace nada de eso.
2. `app/CMakeLists.txt` — `AUTOMOC/AUTORCC/AUTOUIC` ya activos. Registrar un `resources.qrc` no requiere tocar el build, solo añadirlo al target.
3. `app/MainWindow.cpp:137-143` — el shell (sidebar `QListWidget` 180px + `QStackedWidget`).
4. Sistema de tema/paleta — **no existe, hay que crearlo**. Centralizar los colores hardcoded dispersos.

**Color disperso a consolidar:**
- `app/widgets/ActivityCalendarWidget.cpp:6-16` — `categoryColor` (verde/azul/ámbar).
- `app/views/DashboardView.cpp:18-29` — `urgencyColor` (ámbar por opacidad).
- `app/util/SubjectUtil.cpp:6-12` — `colorForName` (8 pastel, persistidos en BD pero **sin renderizar**).
- ~8 `setStyleSheet` inline (TimerWidget, NotesView, SettingsView, StudyView, EventDialog, GoogleCredentialsDialog, NewNoteDialog). El patrón `"color: gray; font-size: 11px;"` se repite 4 veces.
- `app/views/StatsView.cpp:27-34` — `applyPaletteTheme`, única adaptación a tema oscuro existente (Qt Charts). Preservar.

**Tipografía actual:** "Consolas" (mono, dependiente del SO) en `NotesView.cpp:45-47`; 56pt bold en `TimerWidget.cpp:15-18`; "Segoe UI" solo para el icono runtime en `main.cpp:19`.

**Iconografía actual:** solo emojis (`📋📚🌐▶⏸✓✅⚪🔄⚠️`) y puntos pintados a mano (`ActivityCalendarWidget::paintCell`, `CalendarView::legendDot`, `DashboardView` DecorationRole de urgencia).

---

## 3. Arquetipo y sustrato

**Tactical Telemetry & CRT Terminal (Dark).** Sustrato único, sin mezclar (regla brutalist-skill). Fondo off-black (nunca `#000`), texto fósforo, dots como puntos luminosos.

---

## 4. Tokens de color

A centralizar en `app/theme/Theme.h`.

| Token | Hex | Uso |
|---|---|---|
| `kBg` | `#0A0A0A` | fondo base (CRT apagado) |
| `kBgElev` | `#121212` | paneles/celdas |
| `kBgSunken` | `#050505` | inputs hundidos |
| `kFg` | `#EAEAEA` | texto primario (fósforo) |
| `kFgDim` | `#6E6E6E` | metadata/secundario |
| `kFgFaint` | `#3A3A3A` | rejilla/disabled |
| `kRule` | `#1F1F1F` | divisores 1px |
| `kAccent` | `#DA291C` | TrackPoint Red (Pantone 485 C). Único accent: alertas/urgencia crítica/errores/activo. Alternativa vintage: `#B11E3A`. |
| `kPhosphor` | `#4AF626` | opcional, **un único** indicador (sync live). Omitir si no aporta. |

Reglas: cero gradientes, cero sombras suaves, cero translucidez, cero `#000`/`#fff` puros.

---

## 5. Tipografía (empaquetada vía `.qrc`)

- **Display** (cabeceras, timer, números de stats): **Doto** (Google Fonts, OFL, variable, grid 6×10, eje de redondeo para dots circulares estilo NDot). Fallback: **Matrix Sans** (5×7, OFL).
- **Body/datos** (todo lo demás): **JetBrains Mono** (OFL). Reemplaza la dependencia de "Consolas".
- Escala: macro 28-72pt (display), micro 10-14px, tracking `+0.05-0.1em`, **mayúsculas** en metadata/labels.
- Carga en `main.cpp` vía `QFontDatabase::addApplicationFont(":/fonts/...")`.

> Verificar el eje de redondeo de Doto al empaquetar (dots circulares vs pixeles cuadrados). Si no cumple, caer a Matrix Sans.

---

## 6. Sistema de dots semánticos

La firma Nothing. Los dots **no son decoración**: cada dot carga significado. Esto los hace compatibles con la regla "no decorative dots" del taste-skill.

| Dimensión | Codificación | Reemplaza a |
|---|---|---|
| **Categoría** | patrón del dot (Study=sólido, Google=anillo, Local=cruzado) — **no color** | `categoryColor` RGB |
| **Urgencia** | densidad del dot (dithering: grid 4×4, 2→14 puntos según días) | `urgencyColor` por alfa |
| **Estado** | dot lleno=on, hueco=off, rojo=error | emojis `✅⚪⚠️🔄` |
| **Conteo/stats** | barras y cifras como grids de dots | barras Qt Charts planas |
| **Display** | caracteres Doto = glifos de rejilla de puntos | tipografía normal del timer |

---

## 7. Geometría y framing

- `border-radius: 0` en **todo** (QSS global).
- Bordes sólidos 1px (`kRule`) para compartimentos, 2px para divisores estructurales.
- Framing ASCII en cabeceras/labels: `[ DASHBOARD ]`, `< SYNC >`, `///`, `+` en esquinas de rejilla.
- Cero tarjetas flotantes; compartimentalización por layouts + bordes 1px.

---

## 8. Textura

- Overlay global de **scanlines CRT** (QWidget top-level transparente, líneas horizontales repeating 2px al 8% negro, pass-through de eventos). Único, toggleable, estático (sin animación).

---

## 9. Iconografía

- Reemplazo **total** de emojis por glifos dot-grid pintados con `QPainter` (grid 10×10, monocromos).
- Nuevo `DotIcon` util + set de glifos (play, pause, check, warn, sync, task, study, web...).
- Ningún icono como archivo; coherente con el lenguaje de dots.

---

## 10. Mapeo a Pass

| Vista/widget | Transformación |
|---|---|
| **MainWindow sidebar** (`MainWindow.cpp:137`) | Nav mono mayúscula, cada ítem con dot-glyph, activo = barra roja izquierda + dot lleno, inactivo = dot hueco |
| **ActivityCalendarWidget** (`paintCell`) | **Hero**: rejillas de dots por día (patrón=categoría, densidad=actividad). `legendDot` → leyenda dot-grid coherente |
| **TimerWidget** | Countdown en Doto dot-matrix (reloj Nothing), fase con dot de estado |
| **DashboardView** | `urgencyColor` → `urgencyDotDensity()` (dithering); secciones con framing `[ ]` |
| **NotesView** | Editor JetBrains Mono; barra de conflicto → strip rojo ASCII `[ ! CONFLICT ]`; vault label dim mono |
| **StatsView** | Extender `applyPaletteTheme` (ya oscuro) a monocromo; barras como conteo de dots; rojo solo en máximos/alertas |
| **SettingsView** | Estados de conexión = dot (on/off/error) reemplazando emojis; sub-sidebar brutalista |
| **Diálogos** | Framing ASCII, labels mono, inputs `kBgSunken` |

---

## 11. Plan de implementación por fases

### Fase 0 — Fundaciones (sin tocar vistas)
- Crear `app/resources.qrc` + registro en `app/CMakeLists.txt` (`qt_add_resources`). AUTOMOC/AUTORCC ya activos.
- Empaquetar `app/assets/fonts/Doto.ttf` + `JetBrainsMono.ttf` (OFL).
- Crear `app/theme/Theme.h` (tokens de color) + `app/theme/theme.qss` (QSS global brutalista: radio 0, bordes, mono, scrollbars, inputs, listas, botones, groupboxes, tablas).
- `applyTheme(QApplication*)`: instala `QStyle("Fusion")`, fija `QFont`, aplica `qApp->setStyleSheet(...)`.
- `main.cpp`: `addApplicationFont` + `applyTheme` antes de crear `MainWindow`.
- Preservar `StatsView::applyPaletteTheme` (se integra al tema oscuro).

### Fase 1 — Shell
- `MainWindow`: sidebar estilizado vía QSS + delegates para dot-glyph + barra roja de activo. Title bar ASCII.
- Verificar en vivo con la skill `run-pass` que el tema global pinta todo legible **sin haber tocado vistas todavía**.

### Fase 2 — Vistas (una a una)
- Orden: Dashboard → Notes → Study → Stats → Settings → SubjectAdmin → Calendar.
- Por cada una: consolidar los `setStyleSheet` inline en el QSS global (objectNames/clases), framing ASCII en cabeceras, emojis → dot-glyphs, urgencia → dithering.

### Fase 3 — Widgets custom + iconografía
- `ActivityCalendarWidget::paintCell`: rejilla de dots semánticos. `legendDot` → dot-grid legend coherente.
- `TimerWidget`: countdown en Doto dot-matrix, fases con dot de estado.
- `DotIcon` util + set de glifos (play/pause/check/warn/sync/etc.) en grid 10×10, reemplazando todos los emojis.
- `DashboardView::urgencyColor` → `urgencyDotDensity()` (dithering).
- Integrar `colorForName` (paleta dormida en BD) como patrón-de-dot por asignatura en barras de Stats / badges.

### Fase 4 — Textura + pulido
- Overlay de scanlines global (toggleable).
- Audit Pre-Flight adaptado: cero em-dashes en copy, un solo accent, radio 0 consistente, dots solo semánticos, contraste WCAG AA.
- Actualizar `estructura.md` con la nueva capa visual.

---

## 12. Riesgos y cuidados

- **Seguridad:** el rediseño toca `GoogleCredentialsDialog` (maneja OAuth client_id/secret ya enmascarado). Preservar el `QLineEdit::Password` del secret; el restyle no debe romper el enmascarado ni introducir eco de datos sensibles. No se cambia lógica de auth.
- **Fuente Doto:** confirmar el eje de redondeo (dots circulares vs pixeles cuadrados) al empaquetar; si no cumple, caer a Matrix Sans.
- **Legibilidad monocroma:** categorías por patrón de dot (no color) es lo más fiel pero lo menos distinguible; validar contraste/patrón en calendario denso.
- **Qt Charts** tiene theming limitado; el monocromo estricto puede requerir overrides manuales de series.
- **Skill `pruebas-concurrencia-sync`:** si se toca la apertura/cierre de la app o el hilo worker de sync durante el rediseño, ejecutar las pruebas de concurrencia correspondientes.

---

## 13. Pre-Flight Check adaptado (no-web)

Antes de dar cada fase por terminada:

- [ ] Cero em-dashes (`—`/`–`) en copy visible; usar guion `-`.
- [ ] Un único accent (`kAccent`) usado de forma idéntica en toda la app.
- [ ] `border-radius: 0` consistente en todos los widgets.
- [ ] Dots solo semánticos (categoría/urgencia/estado/conteo), no decorativos.
- [ ] Contraste WCAG AA mínimo (texto `kFg` sobre `kBg`).
- [ ] Una sola familia tipográfica por rol (Doto display + JetBrains Mono body), sin mezclar más.
- [ ] Cero emojis residuales en la UI.
- [ ] Sustrato oscuro único, sin secciones que inviertan a claro.
