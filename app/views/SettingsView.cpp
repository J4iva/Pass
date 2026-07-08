// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsView.h"

#include "SubjectAdminView.h"
#include "../widgets/GoogleCredentialsDialog.h"

#include "pass/google/GoogleAuthService.h"
#include "pass/google/GoogleSyncService.h"
#include "pass/settings/AppSettings.h"
#include "pass/sync/GitRunner.h"
#include "pass/sync/GitSyncController.h"

#include <QLocale>

#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace pass;

namespace {

// Etiqueta de subapartado (negrita), para separar secciones dentro de un grupo.
QLabel* sectionHeader(const QString& text) {
    auto* label = new QLabel(text);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

} // namespace

SettingsView::SettingsView(GoogleAuthService& auth, GoogleSyncService* sync,
                           pass::sync::GitSyncController* gitSync, AppSettings* settings,
                           QSqlDatabase db, CalendarService* calendar, QWidget* parent)
    : QWidget(parent), m_auth(auth), m_sync(sync), m_gitSync(gitSync), m_settings(settings),
      m_db(std::move(db)), m_calendar(calendar), m_credStatus(new QLabel),
      m_credButton(new QPushButton), m_accountStatus(new QLabel),
      m_connect(new QPushButton(tr("Conectar cuenta de Google"))),
      m_disconnect(new QPushButton(tr("Desconectar"))),
      m_syncNow(new QPushButton(tr("Sincronizar ahora"))), m_syncStatus(new QLabel),
      m_lastSync(new QLabel), m_lastError(new QLabel) {
    // --- Página Conexiones: scroll con un grupo por tecnología ---
    auto* conexiones = new QWidget;
    auto* conexionesLayout = new QVBoxLayout(conexiones);
    conexionesLayout->addWidget(buildGoogleGroup());
    if (m_gitSync && m_settings)
        conexionesLayout->addWidget(buildGitSyncGroup());
    conexionesLayout->addStretch();

    auto* conexionesScroll = new QScrollArea;
    conexionesScroll->setWidgetResizable(true);
    conexionesScroll->setFrameShape(QFrame::NoFrame);
    conexionesScroll->setWidget(conexiones);

    // --- Página Administración ---
    QWidget* administracion = nullptr;
    if (m_db.isValid() && m_db.isOpen() && m_settings) {
        administracion = new SubjectAdminView(m_db, m_settings, m_calendar);
    } else {
        administracion = new QWidget;
        auto* lay = new QVBoxLayout(administracion);
        auto* msg = new QLabel(tr("Base de datos no disponible."));
        msg->setAlignment(Qt::AlignCenter);
        lay->addWidget(msg);
    }

    // --- Lista lateral interna + páginas ---
    auto* nav = new QListWidget;
    nav->setFixedWidth(150);
    nav->setFrameShape(QFrame::NoFrame);
    nav->addItem(tr("Conexiones"));
    nav->addItem(tr("Administración"));

    auto* pages = new QStackedWidget;
    pages->addWidget(conexionesScroll);
    pages->addWidget(administracion);
    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    nav->setCurrentRow(0);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(nav);
    layout->addWidget(pages, 1);

    // --- Señales de la cuenta de Google ---
    connect(m_credButton, &QPushButton::clicked, this, &SettingsView::editCredentials);
    connect(m_connect, &QPushButton::clicked, this, [this] { m_auth.connectAccount(); });
    connect(m_disconnect, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Desconectar"),
                                  tr("Se borrarán los tokens de esta cuenta y se revocará el "
                                     "acceso en Google. ¿Continuar?")) == QMessageBox::Yes)
            m_auth.disconnectAccount();
    });

    connect(&m_auth, &GoogleAuthService::connected, this, [this] { refreshConnectionState(); });
    connect(&m_auth, &GoogleAuthService::disconnected, this, [this] { refreshConnectionState(); });
    connect(&m_auth, &GoogleAuthService::authError, this, [this](const QString& msg) {
        QMessageBox::warning(this, tr("Google Calendar"), msg);
        refreshConnectionState();
    });

    if (m_sync) {
        connect(m_syncNow, &QPushButton::clicked, m_sync, &GoogleSyncService::syncNow);
        connect(m_sync, &GoogleSyncService::statusChanged, this, [this] { refreshSyncState(); });
        connect(m_sync, &GoogleSyncService::syncFinished, this, [this] { refreshSyncState(); });
        connect(m_sync, &GoogleSyncService::errorOccurred, this, [this] { refreshSyncState(); });
    }

    refreshConnectionState();
    refreshSyncState();
}

// ---------------------------------------------------------------------------
// Conexiones: Google Calendar
// ---------------------------------------------------------------------------

QGroupBox* SettingsView::buildGoogleGroup() {
    auto* group = new QGroupBox(tr("Google"));
    auto* layout = new QVBoxLayout(group);

    // Credenciales de Google Cloud (estado + botón al pop-up).
    layout->addWidget(sectionHeader(tr("Credenciales de Google Cloud")));
    auto* credRow = new QHBoxLayout;
    credRow->addWidget(m_credStatus);
    credRow->addStretch();
    credRow->addWidget(m_credButton);
    layout->addLayout(credRow);

    // Cuenta.
    layout->addWidget(sectionHeader(tr("Estado de la cuenta")));
    layout->addWidget(m_accountStatus);
    auto* accountButtons = new QHBoxLayout;
    accountButtons->addWidget(m_connect);
    accountButtons->addWidget(m_disconnect);
    accountButtons->addStretch();
    layout->addLayout(accountButtons);

    // Sincronización.
    layout->addWidget(sectionHeader(tr("Estado de sincronización")));
    layout->addWidget(m_syncStatus);
    layout->addWidget(m_lastSync);
    layout->addWidget(m_lastError);
    layout->addWidget(m_syncNow, 0, Qt::AlignLeft);

    return group;
}

void SettingsView::editCredentials() {
    GoogleCredentialsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_auth.setClientCredentials(dlg.clientId(), dlg.clientSecret());
    refreshConnectionState();
}

void SettingsView::refreshConnectionState() {
    const bool hasCreds = m_auth.hasClientCredentials();
    const bool connected = m_auth.isConnected();

    m_credStatus->setText(hasCreds ? tr("✓ Credenciales guardadas") : tr("⚪ Sin credenciales"));
    m_credButton->setText(hasCreds ? tr("Cambiar credenciales…") : tr("Añadir credenciales…"));

    m_accountStatus->setText(connected ? tr("Estado: ✅ Conectado")
                                       : tr("Estado: ⚪ Desconectado"));
    m_connect->setEnabled(hasCreds && !connected);
    m_disconnect->setEnabled(connected);
    m_syncNow->setEnabled(m_sync != nullptr && connected);
}

void SettingsView::refreshSyncState() {
    if (!m_sync) {
        m_syncStatus->setText(tr("Estado de sync: —"));
        m_lastSync->setText(tr("Última sincronización: —"));
        m_lastError->setText(QString());
        m_syncNow->setEnabled(false);
        return;
    }

    QString statusText;
    switch (m_sync->status()) {
    case pass::GoogleSyncService::Status::Disconnected:
        statusText = tr("⚪ Sin conectar");
        break;
    case pass::GoogleSyncService::Status::Idle:
        statusText = tr("✅ Al día");
        break;
    case pass::GoogleSyncService::Status::Syncing:
        statusText = tr("🔄 Sincronizando…");
        break;
    case pass::GoogleSyncService::Status::Error:
        statusText = tr("⚠️ Error");
        break;
    }
    m_syncStatus->setText(tr("Estado de sync: %1").arg(statusText));

    const QDateTime last = m_sync->lastSync();
    m_lastSync->setText(last.isValid()
                            ? tr("Última sincronización: %1")
                                  .arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat))
                            : tr("Última sincronización: nunca"));

    const QString err = m_sync->lastError();
    m_lastError->setText(err.isEmpty() ? QString() : tr("Último error: %1").arg(err));

    m_syncNow->setEnabled(m_auth.isConnected() &&
                          m_sync->status() != pass::GoogleSyncService::Status::Syncing);
}

// ---------------------------------------------------------------------------
// Conexiones: sincronización entre dispositivos (GitHub)
// ---------------------------------------------------------------------------

QGroupBox* SettingsView::buildGitSyncGroup() {
    auto* group = new QGroupBox(tr("GitHub (sincronización entre dispositivos)"));

    auto* help = new QLabel(
        tr("Usa un repositorio <b>privado</b> de GitHub para mantener tus estadísticas y notas "
           "sincronizadas entre equipos. Necesitas Git instalado con Git Credential Manager. "
           "Pass <b>nunca</b> guarda tu contraseña ni tokens: la autenticación la gestiona Git. "
           "Guía: docs/github-sync.md."));
    help->setWordWrap(true);

    m_gitRepo = new QLabel;
    m_gitStatus = new QLabel;
    m_gitLastSync = new QLabel;
    m_gitError = new QLabel;
    m_gitError->setWordWrap(true);
    m_gitError->setStyleSheet(QStringLiteral("color: #b00;"));

    m_gitClone = new QPushButton(tr("Clonar repositorio…"));
    m_gitAdopt = new QPushButton(tr("Elegir clon existente…"));
    m_gitSyncNow = new QPushButton(tr("Sincronizar ahora"));
    m_useNotesVault = new QPushButton(tr("Usar la carpeta notes/ del repo como vault"));

    m_deviceName = new QLineEdit(m_settings->syncDeviceName());
    auto* saveName = new QPushButton(tr("Guardar nombre"));
    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(m_deviceName);
    nameRow->addWidget(saveName);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_gitClone);
    buttons->addWidget(m_gitAdopt);
    buttons->addWidget(m_gitSyncNow);
    buttons->addStretch();

    auto* lay = new QVBoxLayout(group);
    lay->addWidget(help);
    lay->addWidget(m_gitRepo);
    lay->addLayout(buttons);
    lay->addWidget(sectionHeader(tr("Estado")));
    lay->addWidget(m_gitStatus);
    lay->addWidget(m_gitLastSync);
    lay->addWidget(m_gitError);
    lay->addWidget(new QLabel(tr("Nombre de este dispositivo:")));
    lay->addLayout(nameRow);
    lay->addWidget(m_useNotesVault, 0, Qt::AlignLeft);

    connect(m_gitClone, &QPushButton::clicked, this, &SettingsView::onCloneClicked);
    connect(m_gitAdopt, &QPushButton::clicked, this, &SettingsView::onAdoptClicked);
    connect(m_gitSyncNow, &QPushButton::clicked, m_gitSync,
            &pass::sync::GitSyncController::syncNow);
    connect(saveName, &QPushButton::clicked, this, &SettingsView::saveDeviceName);
    connect(m_useNotesVault, &QPushButton::clicked, this, &SettingsView::useNotesFolderAsVault);

    connect(m_gitSync, &pass::sync::GitSyncController::statusChanged, this,
            [this] { refreshGitState(); });
    connect(m_gitSync, &pass::sync::GitSyncController::errorOccurred, this,
            [this] { refreshGitState(); });
    connect(m_gitSync, &pass::sync::GitSyncController::cloneFinished, this,
            [this](bool ok, const QString& branch, const QString& error) {
                if (ok) {
                    onGitConnected(branch);
                } else {
                    QMessageBox::warning(this, tr("Sincronización"),
                                         tr("No se pudo conectar el repositorio: %1").arg(error));
                }
                refreshGitState();
            });

    refreshGitState();
    return group;
}

void SettingsView::onCloneClicked() {
    bool ok = false;
    const QString url =
        QInputDialog::getText(this, tr("Clonar repositorio"),
                              tr("URL del repositorio privado de GitHub:\n"
                                 "(https://github.com/usuario/repo o git@github.com:usuario/repo)"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || url.isEmpty())
        return;
    // Valida la URL ANTES de ejecutar git (defensa en profundidad).
    if (!pass::sync::GitRunner::isAllowedRemoteUrl(url)) {
        QMessageBox::warning(this, tr("URL no válida"),
                             tr("Solo se admiten repositorios de GitHub:\n"
                                "https://github.com/usuario/repo o git@github.com:usuario/repo.\n"
                                "No incluyas usuario ni contraseña en la URL."));
        return;
    }
    const QString dest = QFileDialog::getExistingDirectory(
        this, tr("Carpeta donde clonar el repositorio"), QDir::homePath());
    if (dest.isEmpty())
        return;
    // El destino debe estar vacío para clonar dentro de él una subcarpeta del repo.
    const QString repoDir = dest + QStringLiteral("/pass-sync");
    QMessageBox::information(this, tr("Clonando"),
                             tr("Git puede pedirte iniciar sesión en una ventana aparte."));
    m_gitSync->cloneRepo(url, repoDir);
}

void SettingsView::onAdoptClicked() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Elegir clon existente del repositorio"), QDir::homePath());
    if (dir.isEmpty())
        return;
    m_gitSync->adoptExistingClone(dir);
}

void SettingsView::onGitConnected(const QString& branch) {
    m_settings->setSyncRepoPath(m_gitSync->repoDir());
    m_settings->setSyncBranch(branch);
    m_gitSync->setIdentity(m_settings->syncDeviceId(), m_settings->syncDeviceName());
    m_gitSync->start();

    // Heurística de privacidad: si el repo es accesible sin credenciales, es público.
    m_gitSync->checkRepoIsPrivate([this](bool isPrivate) {
        if (!isPrivate)
            QMessageBox::warning(
                this, tr("⚠️ Repositorio público"),
                tr("El repositorio parece ser PÚBLICO: cualquiera podría leer tus datos y "
                   "notas. Hazlo privado en GitHub cuanto antes."));
    });
}

void SettingsView::saveDeviceName() {
    const QString name = m_deviceName->text().trimmed();
    if (name.isEmpty())
        return;
    m_settings->setSyncDeviceName(name);
    m_gitSync->setIdentity(m_settings->syncDeviceId(), name);
}

void SettingsView::useNotesFolderAsVault() {
    if (!m_gitSync->isConfigured()) {
        QMessageBox::information(this, tr("Sincronización"),
                                 tr("Primero conecta un repositorio."));
        return;
    }
    m_settings->setVaultPath(m_gitSync->repoDir());
    m_settings->setVaultSubfolder(QStringLiteral("notes"));
    QMessageBox::information(
        this, tr("Vault actualizado"),
        tr("Las notas se guardarán en la carpeta notes/ del repositorio. Reinicia Pass para "
           "aplicar el cambio."));
}

void SettingsView::refreshGitState() {
    if (!m_gitSync)
        return;
    const bool configured = m_gitSync->isConfigured();
    m_gitRepo->setText(configured ? tr("Repositorio: %1").arg(m_gitSync->repoDir())
                                   : tr("Repositorio: (sin configurar)"));

    QString statusText;
    switch (m_gitSync->status()) {
    case pass::sync::GitSyncService::Status::Disabled:
        statusText = tr("⚪ Sin configurar / Git no disponible");
        break;
    case pass::sync::GitSyncService::Status::Idle:
        statusText = tr("✅ Al día");
        break;
    case pass::sync::GitSyncService::Status::Syncing:
        statusText = tr("🔄 Sincronizando…");
        break;
    case pass::sync::GitSyncService::Status::Warning:
        statusText = tr("⚠️ Pendiente de enviar");
        break;
    case pass::sync::GitSyncService::Status::Error:
        statusText = tr("⚠️ Error");
        break;
    }
    m_gitStatus->setText(tr("Estado: %1").arg(statusText));

    const QDateTime last = m_gitSync->lastSync();
    m_gitLastSync->setText(
        last.isValid()
            ? tr("Última sincronización: %1")
                  .arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat))
            : tr("Última sincronización: nunca"));

    const QString err = m_gitSync->lastError();
    m_gitError->setText(err.isEmpty() ? QString() : tr("Último aviso: %1").arg(err));

    m_gitSyncNow->setEnabled(configured &&
                             m_gitSync->status() != pass::sync::GitSyncService::Status::Syncing);
    m_useNotesVault->setEnabled(configured);
}
