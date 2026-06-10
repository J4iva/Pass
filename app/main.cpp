// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Pass"));
    QApplication::setApplicationName(QStringLiteral("Pass"));

    MainWindow window;
    window.show();
    return QApplication::exec();
}
