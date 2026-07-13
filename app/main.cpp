// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QPainter>

namespace {

// Icono dibujado en runtime (cuadrado, dark tactical, accent rojo): coherente
// con el tema brutalista. Sin binarios en el repo.
QIcon appIcon() {
    constexpr int kSize = 256;
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QColor bg(0x0A, 0x0A, 0x0A);
    const QColor rule(0x6E, 0x6E, 0x6E);
    const QColor accent(0xDA, 0x29, 0x1C);
    const QColor fg(0xEA, 0xEA, 0xEA);

    // Cuerpo cuadrado (border-radius: 0).
    p.setPen(QPen(accent, 4));
    p.setBrush(bg);
    p.drawRect(6, 6, kSize - 12, kSize - 12);

    // Crosshairs en esquinas (marcador de telemetria).
    p.setPen(QPen(rule, 2));
    const int m = 22, c = 10;
    for (const QPoint& corner : {QPoint(m, m), QPoint(kSize - m, m), QPoint(m, kSize - m),
                                 QPoint(kSize - m, kSize - m)}) {
        p.drawLine(corner.x() - c, corner.y(), corner.x() + c, corner.y());
        p.drawLine(corner.x(), corner.y() - c, corner.x(), corner.y() + c);
    }

    // Glifo "P".
    p.setPen(fg);
    QFont font(pass::theme::displayFamily(), 150);
    font.setStyleHint(QFont::Monospace);
    p.setFont(font);
    p.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("P"));
    return QIcon(pixmap);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Pass"));
    QApplication::setApplicationName(QStringLiteral("Pass"));

    // Tema brutalista global: Fusion + fuentes empaquetadas + paleta oscura +
    // QSS. Antes de crear cualquier widget (y del icono, que usa la fuente).
    pass::theme::applyTheme(&app);
    QApplication::setWindowIcon(appIcon());

    MainWindow window;
    window.show();
    return QApplication::exec();
}
