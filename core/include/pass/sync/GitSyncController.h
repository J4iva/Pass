// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/repo/SyncStateRepository.h"
#include "pass/sync/GitSyncService.h"

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

#include <functional>

class QThread;

namespace pass::sync {

// Fachada en el hilo de la GUI sobre GitSyncService. Mueve TODO el pipeline de
// sincronización (operaciones de red git + import/export SQL) a un hilo worker
// para que la interfaz NUNCA se congele, sin reintroducir carreras sobre la BD.
//
// Toda la complejidad de hilos queda encapsulada aquí; el resto de la app habla
// con esta clase como si fuera el servicio de siempre. Reglas (modo contingencia):
//   - El worker (GitSyncService) vive en m_thread y abre su PROPIA conexión
//     SQLite en ese hilo (2ª conexión al mismo fichero). El modo WAL permite que
//     el lector del hilo GUI y el escritor del worker coexistan sin SQLITE_BUSY.
//   - Toda acción de la GUI se REENVÍA al worker en cola (QMetaObject::invokeMethod
//     con Qt::QueuedConnection): nunca se llama a un método del worker en directo.
//   - El estado para pintar (status/lastError/repo/branch) se CACHEA en el hilo
//     GUI desde las señales del worker; los getters jamás tocan al worker ni su
//     conexión. lastSync() se lee de la conexión GUI (solo lectura).
//   - El cierre (shutdownSync) es acotado: push final con presupuesto, cierre de
//     la conexión del worker en su hilo y unión del hilo.
class GitSyncController : public QObject {
    Q_OBJECT

public:
    using Status = GitSyncService::Status;

    // guiDb: conexión del hilo GUI (solo se usa para leer github/last_sync al
    //        pintar; el worker NO la toca).
    // dbPath: ruta del fichero .db que el worker abrirá con su propia conexión.
    GitSyncController(QSqlDatabase guiDb, QString dbPath, QObject* parent = nullptr);
    ~GitSyncController() override;

    // --- Getters cacheados (hilo GUI, sin tocar el worker) ---
    Status status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    QDateTime lastSync() const;
    bool isConfigured() const { return !m_repoDir.isEmpty(); }
    QString branch() const { return m_branch; }
    QString repoDir() const { return m_repoDir; }

    // --- Acciones (se reenvían al worker en cola) ---
    void setIdentity(const QString& deviceId, const QString& deviceName);
    void setRepo(const QString& repoDir, const QString& branch);
    void setNotesDir(const QString& notesDir);
    void setCommandsEnabled(bool enabled);
    void start();
    void cloneRepo(const QString& url, const QString& destDir);
    void adoptExistingClone(const QString& dir);
    void checkRepoIsPrivate(std::function<void(bool isPrivate)> cb);
    void setAllowLocalRemotes(bool allow);

    // Cierre de la app: bloquea (acotado por budgetMs) hasta el push final y une
    // el hilo. Idempotente. Debe llamarse en closeEvent.
    void shutdownSync(int budgetMs);

public slots:
    void syncNow();
    void scheduleAutoPush();

signals:
    void statusChanged(GitSyncService::Status status);
    void errorOccurred(const QString& message);
    void remoteDataApplied();
    void presenceWarning(const QString& deviceName);
    void cloneFinished(bool ok, const QString& branch, const QString& error);

private:
    GitSyncService* m_worker = nullptr; // vive en m_thread; NO usar sus getters
    QThread* m_thread = nullptr;
    SyncStateRepository m_guiState;     // lectura de last_sync en el hilo GUI
    bool m_shutdownDone = false;

    // Estado cacheado en el hilo GUI.
    Status m_status = Status::Disabled;
    QString m_lastError;
    QString m_repoDir;
    QString m_branch = QStringLiteral("main");
    QString m_pendingRepoDir; // destino de un clone/adopt en curso (para cachear al éxito)
};

} // namespace pass::sync
