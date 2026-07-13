// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QPalette>
#include <QStyleFactory>

namespace pass::theme {

namespace {

// Familias resueltas tras cargar las fuentes empaquetadas. Vacias si la fuente
// no esta disponible (applyTheme cae al monoespaciado del sistema).
QString gDisplayFamily;
QString gAltDisplayFamily; // VT323 (alternativa de display conmutable por env)
QString gBodyFamily;

// Carga una fuente empaquetada (.qrc) y devuelve su familia resuelta.
QString loadFont(const QString& resPath) {
    const int id = QFontDatabase::addApplicationFont(resPath);
    if (id < 0)
        return {};
    const auto families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.first();
}

QString readResource(const QString& resPath) {
    QFile f(resPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

bool applyTheme(QApplication* app) {
    // Fusion: base neutra que respeta paleta/QSS (sin cromos nativos de Windows).
    if (auto* style = QStyleFactory::create(QStringLiteral("Fusion")))
        app->setStyle(style);

    // Fuentes empaquetadas. Fallback automatico si faltan.
    gDisplayFamily = loadFont(QStringLiteral(":/fonts/Doto.ttf"));
    gAltDisplayFamily = loadFont(QStringLiteral(":/fonts/VT323.ttf"));
    gBodyFamily = loadFont(QStringLiteral(":/fonts/JetBrainsMono.ttf"));
    if (gBodyFamily.isEmpty())
        gBodyFamily = QStringLiteral("JetBrains Mono");

    // Paleta oscura (Dark Tactical). Cubre lo que el QSS no alcanza del todo:
    // QChart, QCalendarWidget, QMessageBox, ventanas nativas y estados disabled.
    QPalette p;
    p.setColor(QPalette::Window, kBg);
    p.setColor(QPalette::WindowText, kFg);
    p.setColor(QPalette::Base, kBgSunken);
    p.setColor(QPalette::AlternateBase, kBgElev);
    p.setColor(QPalette::Text, kFg);
    p.setColor(QPalette::Button, kBgElev);
    p.setColor(QPalette::ButtonText, kFg);
    p.setColor(QPalette::BrightText, kAccent);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, kFg);
    p.setColor(QPalette::ToolTipBase, kBgElev);
    p.setColor(QPalette::ToolTipText, kFg);
    p.setColor(QPalette::PlaceholderText, kFgDim);
    p.setColor(QPalette::Light, kRule);
    p.setColor(QPalette::Midlight, kRule);
    p.setColor(QPalette::Mid, kRule);
    p.setColor(QPalette::Dark, kBgSunken);
    p.setColor(QPalette::Shadow, kBgSunken);
    p.setColor(QPalette::Link, kAccent);
    p.setColor(QPalette::LinkVisited, kAccent);
    // Disabled: texto atenuado, no clavado en negro.
    p.setColor(QPalette::Disabled, QPalette::WindowText, kFgFaint);
    p.setColor(QPalette::Disabled, QPalette::Text, kFgFaint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, kFgFaint);
    p.setColor(QPalette::Disabled, QPalette::Highlight, kRule);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, kFgDim);
    app->setPalette(p);

    // Fuente global: body mono con tracking mecanico.
    app->setFont(bodyFont(10));

    // QSS global brutalista.
    app->setStyleSheet(readResource(QStringLiteral(":/theme/theme.qss")));

    return !gDisplayFamily.isEmpty();
}

QString displayFamily() {
    // Texto de cabecera: vectorial (Doto) para mayusculas uniformes.
    // (Las pixel-font escaladas desfasan 1px entre letras.) Env override para
    // probar pixel: PASS_DISPLAY_FONT=VT323.
    const QByteArray env = qgetenv("PASS_DISPLAY_FONT");
    const QString name = QString::fromLatin1(env).trimmed();
    if (name.compare("VT323", Qt::CaseInsensitive) == 0 && !gAltDisplayFamily.isEmpty())
        return gAltDisplayFamily;
    if (!gDisplayFamily.isEmpty())
        return gDisplayFamily;
    return bodyFamily();
}

QString bodyFamily() {
    return gBodyFamily.isEmpty() ? QString::fromLatin1(kFontBody) : gBodyFamily;
}

QFont displayFont(int pointSize) {
    QFont f(displayFamily());
    f.setPointSize(qMax(6, pointSize));
    f.setBold(true);
    f.setStyleHint(QFont::Monospace);
    // NoSubpixelAntialias: evita el fringing RGB de ClearType en Windows que
    // hace que algunas letras (especialmente acentos) parezcan de distinto
    // tamaño o peso al tener posiciones de subpixel distintas.
    f.setStyleStrategy(QFont::NoSubpixelAntialias);
    return f;
}

QFont timerFont(int pointSize) {
    // VT323 pixel para el countdown: los digitos son uniformes (sin desfaz).
    QFont f(gAltDisplayFamily.isEmpty() ? QString::fromLatin1(kFontDisplay) : gAltDisplayFamily);
    f.setPointSize(qMax(6, pointSize));
    f.setStyleHint(QFont::Monospace);
    f.setStyleStrategy(QFont::NoSubpixelAntialias);
    return f;
}

QFont bodyFont(int pointSize) {
    QFont f(bodyFamily());
    f.setPointSize(qMax(6, pointSize));
    f.setStyleHint(QFont::Monospace);
    f.setStyleStrategy(QFont::NoSubpixelAntialias);
    // Tracking generoso: simula espaciado de terminal/maquina de escribir.
    f.setLetterSpacing(QFont::PercentageSpacing, 106);
    return f;
}

QLabel* titleLabel(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName("pageTitle");
    QFont f = displayFont(22);
    f.setLetterSpacing(QFont::PercentageSpacing, 104);
    label->setFont(f);
    return label;
}

QLabel* sectionLabel(const QString& text) {
    auto* label = new QLabel(QStringLiteral("[ %1 ]").arg(text.toUpper()));
    label->setObjectName("sectionLabel");
    return label;
}

QString statusDotRich(const char* hex, ushort ch, const QString& text) {
    return QStringLiteral("<span style=\"color:%1\">%2</span>&nbsp;&nbsp;%3")
        .arg(QString::fromLatin1(hex), QString(QChar(ch)), text);
}

} // namespace pass::theme
