// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/repo/SyncStateRepository.h"
#include "pass/sync/GitRunner.h"

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

#include <functional>

class QTimer;

namespace pass::sync {

// Orquesta la sincronización entre dispositivos contra un repo privado de GitHub.
// Imita a GoogleSyncService: timer periódico, estados, sync inicial al arrancar.
//
// Pipeline syncNow() (cadena asíncrona, un solo vuelo + pendiente):
//   1. Preflight: ¿es un repo git? re-validar la URL del remoto.
//   2. exportAll() + escribir presencia (open).
//   3. status --porcelain -> si hay cambios: add -A + commit (commit-first).
//   4. fetch; si hay rama remota: oldHead = HEAD; merge origin/<branch>.
//   5. Conflictos por archivo (nunca destructivo): data/ por updated_at,
//      notes/ conserva ambas, presence/ y manifest -> ours. merge --abort si algo raro.
//   6. diff oldHead..HEAD -- data -> importPaths (o importAll la 1ª vez). remoteDataApplied().
//   7. re-export + commit (si LWW conservó filas locales más nuevas).
//   8. push; si no-fast-forward -> reintenta merge (máx 2) -> si se agota, Warning.
//   9. Idle; persiste github/last_sync.
//
// Seguridad (modo contingencia): toda ejecución de git pasa por GitRunner
// (programa+args, sin shell, timeouts, env endurecido); credenciales 100% en GCM;
// URLs validadas con isAllowedRemoteUrl antes de usarse; mensajes saneados.
class GitSyncService : public QObject {
    Q_OBJECT

public:
    enum class Status { Disabled, Idle, Syncing, Error, Warning };
    Q_ENUM(Status)

    // Modo directo (tests / hilo único): usa la conexión SQLite que se le pasa.
    GitSyncService(QSqlDatabase db, QObject* parent = nullptr);
    // Modo worker (producción detrás de GitSyncController): se construye SIN
    // conexión y abre la suya propia con initDatabase() ya en su hilo, porque una
    // QSqlDatabase debe crearse y usarse en el mismo hilo.
    explicit GitSyncService(QObject* parent = nullptr);
    ~GitSyncService() override;

    Status status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    QDateTime lastSync() const;
    bool isConfigured() const { return !m_repoDir.isEmpty(); }
    QString branch() const { return m_branch; }
    QString repoDir() const { return m_repoDir; }

    // Identidad de este dispositivo (presencia). Llamar antes de start().
    void setIdentity(const QString& deviceId, const QString& deviceName);
    // Configura un clon ya existente (sin clonar). Vacío => no configurado.
    void setRepo(const QString& repoDir, const QString& branch);
    // Carpeta de notas de Pass dentro del vault del usuario (vault/subcarpeta). Si
    // está fijada, sus `.md` se espejan con `notes/` del repo para sincronizarlas.
    // Vacía => no se sincronizan notas (solo datos). Puede vivir fuera del repo.
    void setNotesDir(const QString& notesDir);
    // Activa/desactiva el procesado de la carpeta `command/` del repo (CLI por
    // comando). Default activado.
    void setCommandsEnabled(bool enabled) { m_commandsEnabled = enabled; }

    // Arranca el timer periódico (15 min) y lanza un sync inicial si está configurado.
    void start();

    // Clona url en destDir, prepara el andamiaje (manifest/.gitignore/notes) y hace
    // el primer push. Emite cloneFinished(ok, branch, error).
    void cloneRepo(const QString& url, const QString& destDir);
    // Adopta un clon ya existente: valida que es un repo git con remoto permitido,
    // detecta la rama y deja el servicio configurado. Emite cloneFinished(...).
    void adoptExistingClone(const QString& dir);

    // Heurística de privacidad: ls-remote SIN credenciales. Si funciona, el repo es
    // público (cb(false)); si exige auth, se asume privado (cb(true)).
    void checkRepoIsPrivate(std::function<void(bool isPrivate)> cb);

    // Cierre de la app: push final síncrono y acotado; escribe presencia "closed".
    void shutdownSync(int budgetMs);

    // Solo tests: habilita remotos locales (file://) en la validación.
    void setAllowLocalRemotes(bool allow);

public slots:
    void syncNow();          // sincronización completa (manual o periódica)
    void scheduleAutoPush(); // debounce 30 s tras un cambio local

    // Abre la conexión SQLite propia del worker (modo worker). Debe invocarse YA
    // en el hilo del worker (vía QMetaObject::invokeMethod en cola). Idempotente.
    void initDatabase(const QString& dbPath);
    // Cierra y elimina la conexión propia. Debe invocarse en el hilo del worker
    // antes de pararlo. Idempotente; no-op en modo directo (tests).
    void closeDatabase();

signals:
    void statusChanged(GitSyncService::Status status);
    void errorOccurred(const QString& message); // saneado, no intrusivo
    void remoteDataApplied();                    // la UI debe refrescar
    void presenceWarning(const QString& deviceName);
    void cloneFinished(bool ok, const QString& branch, const QString& error);

private:
    // Cadena del pipeline. Solo fetch y push son asíncronos (red); el resto son
    // operaciones git locales y rápidas, ejecutadas en bloque.
    void runCycle();
    void doFetch();             // async
    void afterFetch();          // merge + import + re-export (local) -> doPush
    void doPush();              // async; gestiona reintento no-ff y cierre
    void finishCycle(Status finalStatus);
    void failCycle(const QString& message);

    bool preflightBlocking();        // rev-parse --is-inside-work-tree + validar remoto
    bool stageLocalBlocking();       // exportAll + presencia(open) + add/commit
    bool mergeAndImportBlocking();   // merge origin/branch + conflictos + import + re-export
    bool exportMirror();             // exportAll() (datos) + espejo de notas vault->repo
    bool writePresence(bool open);   // presence/<id>.json
    // Notas: espejo de `.md` entre el vault de Pass y `notes/` del repo.
    void seedVaultFromRepoNotes();   // unión inicial aditiva (cero pérdida)
    void mirrorNotesToRepo();        // vault -> repo/notes (destructivo, post-unión)
    void mirrorNotesFromRepo();      // repo/notes -> vault (destructivo, tras la fusión)
    // CLI por comando: procesa `command/*.passcmd` (crear asignaturas/tareas/
    // notas/...) y los mueve a `command/processed/`. Solo `create`; idempotente.
    void processPendingCommands();
    void checkPresence();            // emite presenceWarning si procede (una vez)
    bool resolveConflictsBlocking(); // resuelve un merge en conflicto (síncrono, local)
    void commitAllBlockingIfDirty(const QString& message); // add -A + commit si procede
    void setStatus(Status status);
    void persistLastSync();

    QSqlDatabase m_db;
    SyncStateRepository m_state;
    // Hijo de this (parent=this): así moveToThread() lo arrastra al hilo worker y
    // los QProcess/timers que crea GitRunner nacen en ese hilo (sin avisos de
    // afinidad). En modo directo (tests) vive en el hilo del test, como siempre.
    GitRunner* m_git = nullptr;
    QString m_ownConnName; // nombre de la conexión propia (modo worker); vacío = directa
    QTimer* m_timer = nullptr;     // periódico 15 min
    QTimer* m_debounce = nullptr;  // auto-push 30 s

    QString m_repoDir;
    QString m_branch = QStringLiteral("main");
    QString m_notesDir; // carpeta de notas de Pass en el vault (vacía = no sync notas)
    QString m_deviceId;
    QString m_deviceName;

    bool m_commandsEnabled = true;

    Status m_status = Status::Disabled;
    QString m_lastError;

    // Estado del ciclo en vuelo (un solo vuelo a la vez).
    bool m_inFlight = false;
    bool m_pendingSync = false;
    bool m_needFullImport = true;
    bool m_presenceWarned = false;
    bool m_hasRemoteBranch = false;
    QString m_oldHead;
    int m_pushRetries = 0;
};

} // namespace pass::sync
