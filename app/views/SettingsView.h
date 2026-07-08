// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QWidget>

class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace pass {
class AppSettings;
class CalendarService;
class GoogleAuthService;
class GoogleSyncService;
namespace sync {
class GitSyncController;
}
} // namespace pass

// Ajustes organizados en dos apartados navegables por una lista lateral interna:
//   - Conexiones: conexiones por tecnología (Google Calendar y repo de GitHub).
//   - Administración: gestión de asignaturas y temas (ver SubjectAdminView).
// Nunca muestra tokens ni el client_secret guardado, ni la contraseña/token del
// repo (la gestiona Git Credential Manager).
class SettingsView : public QWidget {
    Q_OBJECT

public:
    SettingsView(pass::GoogleAuthService& auth, pass::GoogleSyncService* sync = nullptr,
                 pass::sync::GitSyncController* gitSync = nullptr,
                 pass::AppSettings* settings = nullptr, QSqlDatabase db = {},
                 pass::CalendarService* calendar = nullptr, QWidget* parent = nullptr);

private:
    // --- Conexiones: Google ---
    QGroupBox* buildGoogleGroup();
    void editCredentials();
    void refreshConnectionState();
    void refreshSyncState();

    // --- Conexiones: GitHub ---
    QGroupBox* buildGitSyncGroup();
    void onCloneClicked();
    void onAdoptClicked();
    void onGitConnected(const QString& branch);
    void refreshGitState();
    void saveDeviceName();
    void useNotesFolderAsVault();

    pass::GoogleAuthService& m_auth;
    pass::GoogleSyncService* m_sync;
    pass::sync::GitSyncController* m_gitSync = nullptr;
    pass::AppSettings* m_settings = nullptr;
    QSqlDatabase m_db;
    pass::CalendarService* m_calendar = nullptr;

    QLabel* m_credStatus;
    QPushButton* m_credButton;
    QLabel* m_accountStatus;
    QPushButton* m_connect;
    QPushButton* m_disconnect;
    QPushButton* m_syncNow;
    QLabel* m_syncStatus;
    QLabel* m_lastSync;
    QLabel* m_lastError;

    // GitHub sync.
    QLabel* m_gitRepo = nullptr;
    QLabel* m_gitStatus = nullptr;
    QLabel* m_gitLastSync = nullptr;
    QLabel* m_gitError = nullptr;
    QPushButton* m_gitClone = nullptr;
    QPushButton* m_gitAdopt = nullptr;
    QPushButton* m_gitSyncNow = nullptr;
    QPushButton* m_useNotesVault = nullptr;
    QLineEdit* m_deviceName = nullptr;
};
