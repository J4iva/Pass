// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QApplication>
#include <QPainter>

namespace {

// Icono dibujado en runtime: evita meter binarios en el repositorio.
QIcon appIcon() {
    QPixmap pixmap(256, 256);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(QStringLiteral("#1565c0")));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(8, 8, 240, 240, 48, 48);
    p.setPen(Qt::white);
    QFont font(QStringLiteral("Segoe UI"), 130, QFont::Bold);
    p.setFont(font);
    p.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("P"));
    return QIcon(pixmap);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Pass"));
    QApplication::setApplicationName(QStringLiteral("Pass"));
    QApplication::setWindowIcon(appIcon());

    MainWindow window;
    window.show();
    return QApplication::exec();
}
