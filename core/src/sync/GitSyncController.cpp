// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/GitSyncController.h"

#include <QMetaObject>
#include <QThread>
#include <QTimeZone>

namespace pass::sync {

GitSyncController::GitSyncController(QSqlDatabase guiDb, QString dbPath, QObject* parent)
    : QObject(parent), m_guiState(guiDb) {
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("git-sync"));

    // Worker en modo diferido: abre su propia conexión ya en su hilo.
    m_worker = new GitSyncService();
    m_worker->moveToThread(m_thread);

    // Señales del worker -> caché en el hilo GUI + reemisión. Como el worker vive
    // en otro hilo, estas conexiones son en cola: las lambdas corren en el hilo GUI.
    connect(m_worker, &GitSyncService::statusChanged, this, [this](GitSyncService::Status st) {
        m_status = st;
        emit statusChanged(st);
    });
    connect(m_worker, &GitSyncService::errorOccurred, this, [this](const QString& msg) {
        m_lastError = msg;
        emit errorOccurred(msg);
    });
    connect(m_worker, &GitSyncService::remoteDataApplied, this,
            [this] { emit remoteDataApplied(); });
    connect(m_worker, &GitSyncService::presenceWarning, this,
            [this](const QString& dev) { emit presenceWarning(dev); });
    connect(m_worker, &GitSyncService::cloneFinished, this,
            [this](bool ok, const QString& branch, const QString& error) {
                if (ok) {
                    m_repoDir = m_pendingRepoDir;
                    m_branch = branch.isEmpty() ? QStringLiteral("main") : branch;
                }
                emit cloneFinished(ok, branch, error);
            });

    // Al terminar el hilo: cierra la conexión y destruye el worker EN su propio
    // hilo (así sus QProcess/timers transitorios se matan en el hilo correcto).
    // El orden importa: closeDatabase antes que deleteLater.
    connect(m_thread, &QThread::finished, m_worker, &GitSyncService::closeDatabase);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
    // Abre la conexión del worker EN su hilo (primer evento que procesa).
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, dbPath] { w->initDatabase(dbPath); }, Qt::QueuedConnection);
}

GitSyncController::~GitSyncController() {
    // El worker se autodestruye en su hilo vía finished->deleteLater; aquí solo
    // paramos el hilo (de forma acotada) si shutdownSync no lo hizo ya.
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait(5000);
    }
}

QDateTime GitSyncController::lastSync() const {
    const auto raw = m_guiState.get(QString::fromLatin1(SyncStateRepository::kGithubLastSync));
    if (!raw || raw->isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(*raw, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

// --- Acciones: reenvío en cola al worker -----------------------------------

void GitSyncController::setIdentity(const QString& deviceId, const QString& deviceName) {
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, deviceId, deviceName] { w->setIdentity(deviceId, deviceName); },
        Qt::QueuedConnection);
}

void GitSyncController::setRepo(const QString& repoDir, const QString& branch) {
    // Cachea de inmediato para que isConfigured()/repoDir() reflejen el cambio en
    // el acto (como hacía la versión síncrona); el worker lo aplica en su hilo.
    m_repoDir = repoDir;
    m_branch = branch.isEmpty() ? QStringLiteral("main") : branch;
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, repoDir, branch] { w->setRepo(repoDir, branch); },
        Qt::QueuedConnection);
}

void GitSyncController::setNotesDir(const QString& notesDir) {
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, notesDir] { w->setNotesDir(notesDir); }, Qt::QueuedConnection);
}

void GitSyncController::setCommandsEnabled(bool enabled) {
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, enabled] { w->setCommandsEnabled(enabled); },
        Qt::QueuedConnection);
}

void GitSyncController::start() {
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->start(); }, Qt::QueuedConnection);
}

void GitSyncController::syncNow() {
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->syncNow(); }, Qt::QueuedConnection);
}

void GitSyncController::scheduleAutoPush() {
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->scheduleAutoPush(); },
                              Qt::QueuedConnection);
}

void GitSyncController::cloneRepo(const QString& url, const QString& destDir) {
    m_pendingRepoDir = destDir;
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, url, destDir] { w->cloneRepo(url, destDir); },
        Qt::QueuedConnection);
}

void GitSyncController::adoptExistingClone(const QString& dir) {
    m_pendingRepoDir = dir;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, dir] { w->adoptExistingClone(dir); },
                              Qt::QueuedConnection);
}

void GitSyncController::checkRepoIsPrivate(std::function<void(bool isPrivate)> cb) {
    QMetaObject::invokeMethod(
        m_worker,
        [this, w = m_worker, cb] {
            // Corre en el hilo worker; el resultado se devuelve a la GUI.
            w->checkRepoIsPrivate([this, cb](bool isPrivate) {
                QMetaObject::invokeMethod(
                    this, [cb, isPrivate] { cb(isPrivate); }, Qt::QueuedConnection);
            });
        },
        Qt::QueuedConnection);
}

void GitSyncController::setAllowLocalRemotes(bool allow) {
    QMetaObject::invokeMethod(m_worker, [w = m_worker, allow] { w->setAllowLocalRemotes(allow); },
                              Qt::QueuedConnection);
}

void GitSyncController::shutdownSync(int budgetMs) {
    if (m_shutdownDone)
        return;
    m_shutdownDone = true;
    if (!m_thread || !m_thread->isRunning())
        return;
    // Bloquea (acotado) hasta el push final, igual que el cierre síncrono previo.
    // Las operaciones de red del worker son asíncronas, así que su bucle de
    // eventos atiende este evento con prontitud aunque haya un ciclo en vuelo.
    if (isConfigured())
        QMetaObject::invokeMethod(
            m_worker, [w = m_worker, budgetMs] { w->shutdownSync(budgetMs); },
            Qt::BlockingQueuedConnection);
    // quit() para el bucle; finished dispara closeDatabase + deleteLater en el
    // hilo del worker. wait() bloquea (acotado) hasta que el hilo termina.
    m_thread->quit();
    m_thread->wait(budgetMs + 2000);
}

} // namespace pass::sync
