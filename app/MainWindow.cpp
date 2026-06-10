// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "pass/Version.h"
#include "views/StudyView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
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
    : QMainWindow(parent), m_db(std::make_unique<pass::Database>(pass::Database::defaultPath())),
      m_timer(new pass::SessionTimerService(this)), m_sidebar(new QListWidget),
      m_pages(new QStackedWidget) {
    setWindowTitle(tr("Pass %1").arg(pass::appVersion()));
    resize(1100, 720);

    m_sidebar->setFixedWidth(180);
    m_sidebar->setFrameShape(QFrame::NoFrame);

    const bool dbOk = m_db->isOpen();
    addPage(tr("Dashboard"), placeholderPage(tr("Dashboard — próximamente")));
    addPage(tr("Calendario"), placeholderPage(tr("Calendario — próximamente")));
    addPage(tr("Notas"), placeholderPage(tr("Notas — próximamente")));
    addPage(tr("Estudio"), dbOk ? static_cast<QWidget*>(new StudyView(*m_db, m_timer))
                                : placeholderPage(tr("Base de datos no disponible")));
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

    if (!dbOk) {
        QMessageBox::warning(this, tr("Pass"),
                             tr("No se pudo abrir la base de datos en %1. Algunas funciones "
                                "estarán deshabilitadas.")
                                 .arg(pass::Database::defaultPath()));
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::addPage(const QString& title, QWidget* page) {
    m_sidebar->addItem(title);
    m_pages->addWidget(page);
}
