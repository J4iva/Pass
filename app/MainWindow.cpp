// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "pass/Version.h"
#include "pass/repo/SessionRepository.h"
#include "views/CalendarView.h"
#include "views/DashboardView.h"
#include "views/NotesView.h"
#include "views/StatsView.h"
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
    const auto unavailable = [&] { return placeholderPage(tr("Base de datos no disponible")); };

    if (dbOk)
        m_calendar = new pass::LocalCalendarProvider(m_db->handle(), this);

    addPage(tr("Dashboard"), dbOk ? static_cast<QWidget*>(new DashboardView(*m_db, m_calendar))
                                  : placeholderPage(tr("Dashboard — próximamente")));

    if (dbOk) {
        auto* calendarView = new CalendarView(*m_db, m_calendar);
        addPage(tr("Calendario"), calendarView);
        connect(calendarView, &CalendarView::startSessionRequested, this,
                &MainWindow::startPlannedSession);
    } else {
        addPage(tr("Calendario"), unavailable());
    }

    addPage(tr("Notas"), new NotesView);

    if (dbOk) {
        m_studyView = new StudyView(*m_db, m_timer, m_calendar);
        m_studyPageIndex = m_pages->count();
        addPage(tr("Estudio"), m_studyView);
    } else {
        addPage(tr("Estudio"), unavailable());
    }

    addPage(tr("Estadísticas"), dbOk ? static_cast<QWidget*>(new StatsView(*m_db))
                                     : unavailable());

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

void MainWindow::startPlannedSession(const QUuid& sessionId) {
    if (!m_studyView)
        return;
    pass::SessionRepository sessions(m_db->handle());
    const auto session = sessions.byId(sessionId);
    if (!session) {
        QMessageBox::warning(this, tr("Pass"), tr("La sesión planificada ya no existe."));
        return;
    }
    m_sidebar->setCurrentRow(m_studyPageIndex);
    m_studyView->startPlannedSession(*session);
}
