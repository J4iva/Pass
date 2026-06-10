// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "pass/Version.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>

namespace {

QWidget* placeholderPage(const QString& text) {
    auto* page = new QWidget;
    auto* layout = new QHBoxLayout(page);
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    return page;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_sidebar(new QListWidget), m_pages(new QStackedWidget) {
    setWindowTitle(tr("Pass %1").arg(pass::appVersion()));
    resize(1100, 720);

    m_sidebar->setFixedWidth(180);
    m_sidebar->setFrameShape(QFrame::NoFrame);

    addPage(tr("Dashboard"), placeholderPage(tr("Dashboard — próximamente")));
    addPage(tr("Calendario"), placeholderPage(tr("Calendario — próximamente")));
    addPage(tr("Notas"), placeholderPage(tr("Notas — próximamente")));
    addPage(tr("Estudio"), placeholderPage(tr("Sesiones de estudio — próximamente")));
    addPage(tr("Estadísticas"), placeholderPage(tr("Estadísticas — próximamente")));

    connect(m_sidebar, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_sidebar->setCurrentRow(0);

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_sidebar);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);
}

void MainWindow::addPage(const QString& title, QWidget* page) {
    m_sidebar->addItem(title);
    m_pages->addWidget(page);
}
