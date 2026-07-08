// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "pass/Version.h"
#include "pass/notes/VaultService.h"
#include "pass/notes/VaultWatcher.h"
#include "views/CalendarView.h"
#include "views/DashboardView.h"
#include "views/NotesView.h"
#include "views/SettingsView.h"
#include "views/StatsView.h"
#include "views/StudyView.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QStackedWidget>
#include <QUrl>

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
      m_tokenStore(std::make_unique<pass::WinCredTokenStore>()),
      m_timer(new pass::SessionTimerService(this)), m_sidebar(new QListWidget),
      m_pages(new QStackedWidget) {
    setWindowTitle(tr("Pass %1").arg(pass::appVersion()));
    resize(1100, 720);

    m_sidebar->setFixedWidth(180);
    m_sidebar->setFrameShape(QFrame::NoFrame);

    const bool dbOk = m_db->isOpen();
    const auto unavailable = [&] { return placeholderPage(tr("Base de datos no disponible")); };

    // Cadena de Google Calendar: el servicio de auth no necesita la DB; el cliente,
    // el sync y el router sí (espejan eventos en la tabla `events`). El router
    // implementa CalendarProvider, así que las vistas no se enteran del cambio.
    m_auth = new pass::GoogleAuthService(*m_tokenStore, this);
    connect(m_auth, &pass::GoogleAuthService::openAuthorizationUrl, this,
            [](const QUrl& url) { QDesktopServices::openUrl(url); });

    if (dbOk) {
        m_client = new pass::GoogleCalendarClient(*m_auth, this);
        m_sync = new pass::GoogleSyncService(m_db->handle(), *m_client, *m_auth, this);
        m_calendar = new pass::CalendarService(m_db->handle(), m_sync, this);
        connect(m_calendar, &pass::CalendarService::syncErrorOccurred, this,
                [this](const QString& msg) { QMessageBox::warning(this, tr("Google Calendar"), msg); });

        // Sincronización entre dispositivos (repo de GitHub). El pipeline corre en
        // un hilo worker (GitSyncController) para no congelar la GUI; el worker abre
        // su propia conexión SQLite al mismo fichero (WAL).
        m_gitSync = new pass::sync::GitSyncController(m_db->handle(),
                                                     pass::Database::defaultPath(), this);
        m_gitSync->setIdentity(m_settings.syncDeviceId(), m_settings.syncDeviceName());
        m_gitSync->setCommandsEnabled(m_settings.commandsEnabled());
        if (!m_settings.syncRepoPath().isEmpty())
            m_gitSync->setRepo(m_settings.syncRepoPath(), m_settings.syncBranch());
        connect(m_gitSync, &pass::sync::GitSyncController::remoteDataApplied, this,
                &MainWindow::refreshDataViews);
        connect(m_gitSync, &pass::sync::GitSyncController::presenceWarning, this,
                [this](const QString& device) {
                    // Aviso no bloqueante: la fusión es automática (LWW por registro).
                    auto* box = new QMessageBox(
                        QMessageBox::Information, tr("Sincronización"),
                        tr("Parece que Pass está abierto en «%1». No pasa nada: los cambios se "
                           "fusionan automáticamente.")
                            .arg(device),
                        QMessageBox::Ok, this);
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->setModal(false);
                    box->show();
                });

        // Notas: Pass espeja SU carpeta de notas (vault/subcarpeta) con notes/ del
        // repo, viva donde viva el vault. Editar notas no toca la BD, así que un
        // watcher propio programa el push (con debounce) cuando cambian.
        if (m_gitSync->isConfigured() && !m_settings.vaultPath().isEmpty()) {
            const pass::VaultService vault(m_settings.vaultPath(), m_settings.vaultSubfolder());
            const QString notesDir = vault.notesDir();
            m_gitSync->setNotesDir(notesDir);
            QDir().mkpath(notesDir); // para poder vigilarla aunque aún no haya notas
            m_notesWatcher = new pass::VaultWatcher(this);
            m_notesWatcher->watch(notesDir);
            connect(m_notesWatcher, &pass::VaultWatcher::vaultChanged, m_gitSync,
                    &pass::sync::GitSyncController::scheduleAutoPush);
            connect(m_notesWatcher, &pass::VaultWatcher::noteChanged, m_gitSync,
                    &pass::sync::GitSyncController::scheduleAutoPush);
        }
    }

    m_dashboardView = dbOk ? new DashboardView(*m_db, m_calendar) : nullptr;
    addPage(tr("Dashboard"), dbOk ? static_cast<QWidget*>(m_dashboardView)
                                  : placeholderPage(tr("Dashboard — próximamente")));

    if (dbOk) {
        m_calendarView = new CalendarView(*m_db, m_calendar);
        addPage(tr("Calendario"), m_calendarView);
    } else {
        addPage(tr("Calendario"), unavailable());
    }

    addPage(tr("Notas"), new NotesView(dbOk ? m_db.get() : nullptr));

    if (dbOk) {
        m_studyView = new StudyView(*m_db, m_timer, m_calendar);
        addPage(tr("Sesiones"), m_studyView);
    } else {
        addPage(tr("Sesiones"), unavailable());
    }

    m_statsView = dbOk ? new StatsView(*m_db) : nullptr;
    addPage(tr("Estadísticas"), dbOk ? static_cast<QWidget*>(m_statsView) : unavailable());

    addPage(tr("Ajustes"),
            new SettingsView(*m_auth, m_sync, m_gitSync, &m_settings,
                             dbOk ? m_db->handle() : QSqlDatabase(), m_calendar));

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

    // Arranca el timer de sincronización y lanza una primera pasada si hay cuenta.
    if (m_sync)
        m_sync->start();
    if (m_gitSync && m_gitSync->isConfigured())
        m_gitSync->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    // Sesión en marcha al cerrar: se interrumpe guardando la posición (fase +
    // segundos) para poder retomarla. interrupt() emite finished(), que StudyView
    // persiste de forma síncrona antes de que se destruyan las vistas.
    if (m_timer->state() == pass::SessionTimerService::State::Running ||
        m_timer->state() == pass::SessionTimerService::State::Paused)
        m_timer->interrupt();
    // Push final acotado: si no llega a tiempo, queda commiteado en local y sube
    // en el próximo arranque. No bloquea más allá del presupuesto.
    if (m_gitSync && m_gitSync->isConfigured())
        m_gitSync->shutdownSync(8000);
    QMainWindow::closeEvent(event);
}

void MainWindow::refreshDataViews() {
    if (m_dashboardView)
        m_dashboardView->refresh();
    if (m_statsView)
        m_statsView->refresh();
    if (m_calendarView)
        m_calendarView->refresh();
}

void MainWindow::addPage(const QString& title, QWidget* page) {
    m_sidebar->addItem(title);
    m_pages->addWidget(page);
}
