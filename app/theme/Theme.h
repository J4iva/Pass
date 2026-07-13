// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QFont;
class QLabel;

namespace pass::theme {

// ---------------------------------------------------------------------------
// Sustrato: Dark Tactical / CRT Terminal.
// Politica: monocromo + un unico rojo TrackPoint. Cero gradientes, cero
// sombras suaves, cero translucidez, cero #000/#fff puros. Los tokens hex
// duplican a los QColor porque QSS no los lee: mantener ambos en sincronia.
// ---------------------------------------------------------------------------

// Fondo
inline const QColor kBg{0x0A, 0x0A, 0x0A};        // fondo base (CRT apagado)
inline const QColor kBgElev{0x12, 0x12, 0x12};    // paneles/celdas
inline const QColor kBgSunken{0x05, 0x05, 0x05};  // inputs hundidos
// Texto (fosforo)
inline const QColor kFg{0xEA, 0xEA, 0xEA};         // primario
inline const QColor kFgDim{0x6E, 0x6E, 0x6E};      // metadata/secundario
inline const QColor kFgFaint{0x3A, 0x3A, 0x3A};    // rejilla/disabled
// Estructura
inline const QColor kRule{0x1F, 0x1F, 0x1F};       // divisores 1px
// Accent (unico): alertas/urgencia critica/errores/activo
inline const QColor kAccent{0xDA, 0x29, 0x1C};     // TrackPoint Red (Pantone 485 C)
inline const QColor kPhosphor{0x4A, 0xF6, 0x26};   // sync live (uso unico, opcional)

// Hex para QSS (sin '#000'/'#fff' puros)
inline constexpr const char* kBgHex = "#0A0A0A";
inline constexpr const char* kBgElevHex = "#121212";
inline constexpr const char* kBgSunkenHex = "#050505";
inline constexpr const char* kFgHex = "#EAEAEA";
inline constexpr const char* kFgDimHex = "#6E6E6E";
inline constexpr const char* kFgFaintHex = "#3A3A3A";
inline constexpr const char* kRuleHex = "#1F1F1F";
inline constexpr const char* kAccentHex = "#DA291C";
inline constexpr const char* kPhosphorHex = "#4AF626";

// Familias tipograficas (empaquetadas via .qrc; ver applyTheme para el fallback).
inline constexpr const char* kFontDisplay = "VT323";        // terminal CRT (mayusculas uniformes)
inline constexpr const char* kFontDisplayDoto = "Doto";     // alternativa Nothing dots (conmutable)
inline constexpr const char* kFontBody = "JetBrains Mono";  // datos/cuerpo

// Instala el tema brutalista en la app: estilo Fusion, fuentes empaquetadas,
// paleta oscura y QSS global. Devuelve true si las fuentes cargaron correctamente.
bool applyTheme(QApplication* app);

// Familia resuelta de display/body tras applyTheme (vacia si no se ha llamado
// y la fuente no cargo). Fallback automatico a monoespaciado del sistema.
QString displayFamily();
QString bodyFamily();

// Fuentes listas para usar.
//   displayFont -> texto de cabecera (JetBrains Mono Bold, vectorial: mayusculas
//                  uniformes; las pixel-font escaladas desfasan 1px entre letras).
//   timerFont   -> countdown (VT323 pixel, solo digitos: uniformes).
//   bodyFont    -> datos/metadata (JetBrains Mono, tracking +0.06em).
QFont displayFont(int pointSize = 28);
QFont timerFont(int pointSize = 64);
QFont bodyFont(int pointSize = 10);

// Fabricas de cabeceras con framing ASCII brutalista (objectNames con estilo QSS).
//   titleLabel     -> titulo de pagina (display, mayusculas).
//   sectionLabel   -> "[ TEXT ]" de subseccion (tenue, con regla inferior).
QLabel* titleLabel(const QString& text);
QLabel* sectionLabel(const QString& text);

// Dot de estado como texto rico (para QLabel): pinta el caracter `ch` colorea en
// `hex` seguido del texto. Reemplaza emojis de estado por dots semánticos.
//   on/idle -> kFgHex + 0x25CF (●)   off -> kFgFaintHex + 0x25CB (○)
//   error  -> kAccentHex + 0x25CF   live/sync -> kPhosphorHex + 0x25CF
QString statusDotRich(const char* hex, ushort ch, const QString& text);

} // namespace pass::theme
