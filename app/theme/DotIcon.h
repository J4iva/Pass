// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Theme.h"

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QPainter;
class QRect;

namespace pass::theme {

// ---------------------------------------------------------------------------
// DotIcon: glifos pintados como rejillas de puntos 10x10 (lenguaje Nothing).
// Cada dot carga significado (no son decoracion): un glifo es un icono de
// navegacion/accion; un dot de estado codifica on/off/error.
// ---------------------------------------------------------------------------

enum class Glyph {
    // Navegacion
    Grid,     // dashboard / vista general
    Calendar, // calendario
    Note,     // nota / documento
    Clock,    // sesion / temporizador
    Bars,     // estadisticas
    Sliders,  // ajustes
    // Accion
    Play,
    Pause,
    Check,
    Warn,
    Sync,
    Task,  // entrega / tarea
    Study, // sesion de estudio (libro)
    Web,   // google / web (globo)
    Plus,
    Edit,
    Trash,
    Info,
};

// Glifo como pixmap cuadrado de lado `px`, monocromo en `color`.
QPixmap glyphPixmap(Glyph g, int px, const QColor& color = kFg);
QIcon glyphIcon(Glyph g, int px, const QColor& color = kFg);

// Pinta el glifo centrado y escalado dentro de `rect`.
void paintGlyph(QPainter* p, Glyph g, const QRect& rect, const QColor& color);

// Dots de estado (geometria simple, no grid 10x10).
//   On    -> circulo lleno (fosforo)
//   Off   -> anillo hueco (tenue)
//   Error -> circulo lleno rojo (accent)
enum class StateDot { On, Off, Error };
QPixmap statePixmap(StateDot s, int px);
void paintStateDot(QPainter* p, StateDot s, const QRect& rect);

} // namespace pass::theme
