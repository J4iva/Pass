// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/GitSyncService.h"

#include "pass/sync/DataChangeNotifier.h"
#include "pass/sync/SyncExporter.h"
#include "pass/sync/SyncImporter.h"

#include "pass/command/CommandParser.h"
#include "pass/command/CommandProcessor.h"

#include <QAtomicInt>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSqlQuery>
#include <QTimeZone>
#include <QTimer>

namespace pass::sync {

namespace {

constexpr int kPeriodicMs = 15 * 60 * 1000; // 15 min
constexpr int kDebounceMs = 30 * 1000;      // 30 s tras un cambio local
constexpr int kNetTimeoutMs = 120 * 1000;   // fetch/push/merge con remoto
constexpr int kPresenceFreshSecs = 45 * 60; // 3 latidos perdidos

// --- CLI por comando: límites de seguridad (modo contingencia) ----------------
// La carpeta command/ es entrada externa que se aplica automáticamente. Limitar
// tamaño y número por ciclo evita avalanchas y ficheros gigantes maliciosos.
constexpr qint64 kMaxCommandBytes = 8 * 1024;     // 8 KiB por .passcmd
constexpr int kMaxCommandsPerCycle = 200;          // tope de comandos por pull

QString nowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// Identidad de commit pasada en línea: los commits funcionan aunque el clon no
// tenga user.name/user.email configurados (típico en un clon recién hecho).
QStringList withIdentity(QStringList tail) {
    QStringList args = {QStringLiteral("-c"), QStringLiteral("user.name=Pass"),
                        QStringLiteral("-c"), QStringLiteral("user.email=pass@localhost")};
    return args + tail;
}

QDateTime parseTimestamp(const QByteArray& json) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    QString ts = o.value(QStringLiteral("updated_at")).toString();
    if (ts.isEmpty())
        ts = o.value(QStringLiteral("deleted_at")).toString();
    QDateTime dt = QDateTime::fromString(ts, Qt::ISODate);
    if (dt.isValid())
        dt.setTimeZone(QTimeZone::utc());
    return dt;
}

// --- espejo de notas (.md) entre el vault de Pass y notes/ del repo -----------

// Rutas relativas de todos los `.md` bajo `dir` (recursivo).
QStringList relMdFiles(const QString& dir) {
    QStringList out;
    if (!QDir(dir).exists())
        return out;
    const QString base = QDir(dir).absolutePath();
    QDirIterator it(base, {QStringLiteral("*.md")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        out << it.fileInfo().absoluteFilePath().mid(base.length() + 1);
    }
    return out;
}

bool sameContent(const QString& a, const QString& b) {
    QFile fb(b);
    if (!fb.exists())
        return false;
    QFile fa(a);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    return fa.readAll() == fb.readAll();
}

void copyOverwrite(const QString& src, const QString& dst) {
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    QFile::copy(src, dst);
}

// Espejo destructivo de `.md`: dst pasa a contener exactamente los `.md` de src
// (copia los que difieren, borra los que sobran). No toca ficheros no-`.md`
// (p. ej. `.gitkeep`). Si src y dst son la misma carpeta, es un no-op.
void mirrorMd(const QString& src, const QString& dst) {
    if (!QDir(src).exists())
        return;
    QDir().mkpath(dst);
    const QStringList srcFiles = relMdFiles(src);
    const QSet<QString> srcSet(srcFiles.cbegin(), srcFiles.cend());
    for (const QString& rel : srcFiles) {
        const QString s = src + QLatin1Char('/') + rel;
        const QString d = dst + QLatin1Char('/') + rel;
        if (!sameContent(s, d))
            copyOverwrite(s, d);
    }
    for (const QString& rel : relMdFiles(dst)) {
        if (!srcSet.contains(rel))
            QFile::remove(dst + QLatin1Char('/') + rel);
    }
}

} // namespace

GitSyncService::GitSyncService(QSqlDatabase db, QObject* parent)
    : QObject(parent), m_db(db), m_state(db), m_git(new GitRunner(QString(), this)) {
    connect(&DataChangeNotifier::instance(), &DataChangeNotifier::changed, this,
            &GitSyncService::scheduleAutoPush);
}

GitSyncService::GitSyncService(QObject* parent)
    : QObject(parent), m_state(QSqlDatabase()), m_git(new GitRunner(QString(), this)) {
    // La conexión se abre en initDatabase() ya dentro del hilo del worker.
    connect(&DataChangeNotifier::instance(), &DataChangeNotifier::changed, this,
            &GitSyncService::scheduleAutoPush);
}

GitSyncService::~GitSyncService() {
    closeDatabase(); // defensivo: normalmente el controller ya la cerró en su hilo
}

void GitSyncService::initDatabase(const QString& dbPath) {
    if (!m_ownConnName.isEmpty())
        return; // idempotente
    static QAtomicInt counter;
    m_ownConnName = QStringLiteral("pass_sync_conn_%1").arg(counter.fetchAndAddRelaxed(1));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_ownConnName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        QSqlDatabase::removeDatabase(m_ownConnName);
        m_ownConnName.clear();
        m_lastError = tr("No se pudo abrir la base de datos de sincronización.");
        setStatus(Status::Error);
        return;
    }
    // foreign_keys es por conexión; busy_timeout evita SQLITE_BUSY frente al
    // lector del hilo GUI. El fichero ya está en WAL (lo fijó la conexión GUI).
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    m_db = db;
    m_state = SyncStateRepository(db);
}

void GitSyncService::closeDatabase() {
    if (m_ownConnName.isEmpty())
        return;
    const QString name = m_ownConnName;
    m_ownConnName.clear();
    // Suelta las copias del handle ANTES de removeDatabase para no dejar
    // referencias colgando (Qt avisaría).
    m_db = QSqlDatabase();
    m_state = SyncStateRepository(QSqlDatabase());
    {
        QSqlDatabase db = QSqlDatabase::database(name, /*open=*/false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

void GitSyncService::setAllowLocalRemotes(bool allow) {
    m_git->setAllowLocalRemotes(allow);
}

void GitSyncService::setIdentity(const QString& deviceId, const QString& deviceName) {
    m_deviceId = deviceId;
    m_deviceName = deviceName;
}

void GitSyncService::setRepo(const QString& repoDir, const QString& branch) {
    m_repoDir = repoDir;
    m_branch = branch.isEmpty() ? QStringLiteral("main") : branch;
    m_git->setRepoDir(repoDir);
    m_needFullImport = true;
    if (!isConfigured())
        setStatus(Status::Disabled);
}

void GitSyncService::setNotesDir(const QString& notesDir) {
    m_notesDir = notesDir;
}

void GitSyncService::mirrorNotesToRepo() {
    if (!m_notesDir.isEmpty())
        mirrorMd(m_notesDir, m_repoDir + QStringLiteral("/notes"));
}

void GitSyncService::mirrorNotesFromRepo() {
    if (!m_notesDir.isEmpty())
        mirrorMd(m_repoDir + QStringLiteral("/notes"), m_notesDir);
}

// Unión inicial (cero pérdida): trae al vault las notas del repo que falten; ante
// un conflicto de contenido conserva la local intacta y añade la remota como copia.
// El espejo destructivo posterior (vault->repo) ya no puede borrar nada del repo,
// porque el vault contiene su unión.
void GitSyncService::seedVaultFromRepoNotes() {
    if (m_notesDir.isEmpty())
        return;
    const QString repoNotes = m_repoDir + QStringLiteral("/notes");
    if (!QDir(repoNotes).exists())
        return;
    QDir().mkpath(m_notesDir);
    for (const QString& rel : relMdFiles(repoNotes)) {
        const QString s = repoNotes + QLatin1Char('/') + rel;
        const QString d = m_notesDir + QLatin1Char('/') + rel;
        if (!QFileInfo::exists(d)) {
            copyOverwrite(s, d);
        } else if (!sameContent(s, d)) {
            const QFileInfo fi(d);
            const QString alt = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                                QStringLiteral(" (de otro dispositivo).md");
            if (!QFileInfo::exists(alt))
                copyOverwrite(s, alt);
        }
    }
}

// CLI por comando: procesa los `command/*.passcmd` pendientes y los mueve a
// `command/processed/`. Se invoca en el hilo worker tras cada fusión (y en el
// primer ciclo tras el import), antes de los espejos de notas, para que:
//   - las inserciones en BD las publique el exportMirror del mismo ciclo;
//   - las notas creadas en `notes/` las traiga al vault el mirrorNotesFromRepo /
//     seedVaultFromRepoNotes inmediatamente posterior (y así el espejo
//     destructivo vault->repo/notes no las borre).
// Seguridad (modo contingencia): límites de tamaño/número; un comando que falla
// (sintaxis o semántica) se queda en sitio para revisión y NO aborta el ciclo.
void GitSyncService::processPendingCommands() {
    if (!m_commandsEnabled)
        return;
    const QString cmdDir = m_repoDir + QStringLiteral("/command");
    if (!QDir(cmdDir).exists())
        return;

    const QString processedDir = cmdDir + QStringLiteral("/processed");
    QDir().mkpath(processedDir);

    QDirIterator it(cmdDir, {QStringLiteral("*.passcmd")}, QDir::Files); // no recursivo
    int processed = 0;
    while (it.hasNext() && processed < kMaxCommandsPerCycle) {
        it.next();
        const QFileInfo fi(it.filePath());

        // Fichero demasiado grande: se ignora (no se mueve, no se procesa) por
        // seguridad. Queda en sitio para revisión manual.
        if (fi.size() > kMaxCommandBytes)
            continue;

        QFile f(fi.filePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        f.close();

        const command::ParseResult pr = command::parse(text, fi.fileName());
        if (!pr.ok)
            continue; // mal formado: se queda en sitio

        command::CommandProcessor processor(m_db, m_repoDir);
        const command::CommandResult res = processor.process(pr.command);
        if (res.status == command::CommandStatus::Failed)
            continue; // error semántico: se queda en sitio para revisión

        // Applied o Skipped: se da por bueno y se mueve a processed/ (viaja en el
        // mismo commit/push => los demás dispositivos no lo reprocesan).
        const QString dest = processedDir + QLatin1Char('/') + fi.fileName();
        QFile::remove(dest);
        if (QFile::rename(fi.filePath(), dest))
            ++processed;
    }
}

QDateTime GitSyncService::lastSync() const {
    const auto raw = m_state.get(QString::fromLatin1(SyncStateRepository::kGithubLastSync));
    if (!raw || raw->isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(*raw, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

void GitSyncService::setStatus(Status status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(m_status);
}

void GitSyncService::persistLastSync() {
    m_state.set(QString::fromLatin1(SyncStateRepository::kGithubLastSync), nowIso());
}

void GitSyncService::start() {
    if (!isConfigured()) {
        setStatus(Status::Disabled);
        return;
    }
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kPeriodicMs);
        connect(m_timer, &QTimer::timeout, this, &GitSyncService::syncNow);
    }
    m_timer->start();
    syncNow();
}

void GitSyncService::scheduleAutoPush() {
    if (!isConfigured())
        return;
    if (!m_debounce) {
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(kDebounceMs);
        connect(m_debounce, &QTimer::timeout, this, &GitSyncService::syncNow);
    }
    m_debounce->start(); // reinicia el debounce
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

void GitSyncService::syncNow() {
    if (!isConfigured()) {
        setStatus(Status::Disabled);
        return;
    }
    if (m_inFlight) {
        m_pendingSync = true;
        return;
    }
    m_inFlight = true;
    m_pushRetries = 0;
    setStatus(Status::Syncing);
    runCycle();
}

void GitSyncService::runCycle() {
    if (!preflightBlocking()) {
        failCycle(m_lastError);
        return;
    }
    // Primer ciclo de un clon: importa los datos del árbol clonado ANTES de exportar.
    // Si no, exportMirror borraría del espejo los datos del clon (la BD local podría
    // estar vacía en un dispositivo recién instalado) y se perderían.
    if (m_needFullImport) {
        SyncImporter importer(m_db, m_repoDir);
        const auto res = importer.importAll();
        if (!res.ok) {
            failCycle(res.error.isEmpty() ? tr("Error al importar datos remotos.") : res.error);
            return;
        }
        processPendingCommands();   // command/*.passcmd (1er ciclo: del clon)
        seedVaultFromRepoNotes(); // unión aditiva de notas antes del primer espejo
        m_needFullImport = false;
        if (res.applied)
            emit remoteDataApplied();
    }
    if (!stageLocalBlocking()) {
        failCycle(tr("No se pudo preparar el espejo local para sincronizar."));
        return;
    }
    doFetch();
}

bool GitSyncService::preflightBlocking() {
    const auto inside = m_git->runBlocking({QStringLiteral("rev-parse"),
                                           QStringLiteral("--is-inside-work-tree")});
    if (!inside.started) {
        setStatus(Status::Disabled);
        m_lastError = tr("Git no está disponible en el sistema.");
        return false;
    }
    if (!inside.ok || inside.stdOut.trimmed() != QStringLiteral("true")) {
        m_lastError = tr("La carpeta de sincronización no es un repositorio git.");
        return false;
    }
    const auto remote = m_git->runBlocking(
        {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
    if (!remote.ok) {
        m_lastError = tr("El repositorio no tiene configurado un remoto 'origin'.");
        return false;
    }
    if (!GitRunner::isAllowedRemoteUrl(remote.stdOut.trimmed(), m_git->allowLocalRemotes())) {
        m_lastError = tr("La URL del remoto no está permitida.");
        return false;
    }
    return true;
}

bool GitSyncService::exportMirror() {
    SyncExporter exporter(m_db, m_repoDir);
    if (!exporter.exportAll())
        return false;
    mirrorNotesToRepo(); // notas del vault -> notes/ del repo (no-op si no hay vault)
    return true;
}

bool GitSyncService::stageLocalBlocking() {
    if (!exportMirror())
        return false;
    writePresence(/*open=*/true);
    commitAllBlockingIfDirty(tr("Pass: cambios locales %1").arg(nowIso()));
    return true;
}

void GitSyncService::commitAllBlockingIfDirty(const QString& message) {
    m_git->runBlocking({QStringLiteral("add"), QStringLiteral("-A")});
    const auto status = m_git->runBlocking({QStringLiteral("status"), QStringLiteral("--porcelain")});
    if (status.ok && !status.stdOut.trimmed().isEmpty())
        m_git->runBlocking(withIdentity({QStringLiteral("commit"), QStringLiteral("-m"), message}));
}

void GitSyncService::doFetch() {
    m_git->run(
        {QStringLiteral("fetch"), QStringLiteral("origin")},
        [this](const GitRunner::Result& r) {
            if (!r.started) {
                setStatus(Status::Disabled);
                failCycle(tr("Git no está disponible en el sistema."));
                return;
            }
            if (!r.ok) {
                // Sin conexión: los commits locales quedan y subirán en el próximo ciclo.
                m_lastError = tr("Sin conexión con el remoto; se reintentará.");
                finishCycle(Status::Warning);
                return;
            }
            afterFetch();
        },
        kNetTimeoutMs);
}

void GitSyncService::afterFetch() {
    const auto ref = m_git->runBlocking({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                        QStringLiteral("--quiet"),
                                        QStringLiteral("origin/") + m_branch});
    m_hasRemoteBranch = ref.ok && !ref.stdOut.trimmed().isEmpty();

    if (m_hasRemoteBranch && !mergeAndImportBlocking()) {
        failCycle(m_lastError.isEmpty() ? tr("No se pudo fusionar con el remoto.") : m_lastError);
        return;
    }
    doPush();
}

bool GitSyncService::mergeAndImportBlocking() {
    const auto head = m_git->runBlocking({QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    m_oldHead = head.ok ? head.stdOut.trimmed() : QString();

    auto merge = m_git->runBlocking(
        withIdentity({QStringLiteral("merge"), QStringLiteral("--no-edit"),
                      QStringLiteral("origin/") + m_branch}),
        kNetTimeoutMs);
    if (!merge.ok && (merge.stdErr + merge.stdOut).contains(QStringLiteral("unrelated histories"))) {
        // Onboarding: dos dispositivos inicializaron la rama por separado. El repo
        // es del propio usuario (URL en whitelist), así que unimos las dos raíces
        // una vez; a partir de ahí comparten historia.
        merge = m_git->runBlocking(
            withIdentity({QStringLiteral("merge"), QStringLiteral("--no-edit"),
                          QStringLiteral("--allow-unrelated-histories"),
                          QStringLiteral("origin/") + m_branch}),
            kNetTimeoutMs);
    }
    if (!merge.ok) {
        const auto unmerged =
            m_git->runBlocking({QStringLiteral("ls-files"), QStringLiteral("--unmerged")});
        const bool isConflict = !unmerged.stdOut.trimmed().isEmpty();
        if (!isConflict || !resolveConflictsBlocking()) {
            m_git->runBlocking({QStringLiteral("merge"), QStringLiteral("--abort")});
            m_lastError = tr("Conflicto de fusión no resuelto; estado local intacto.");
            return false;
        }
    }

    checkPresence();

    // Importa solo lo que trajo la fusión (diff oldHead..HEAD). El import inicial
    // completo ya se hizo en runCycle, así que aquí siempre es incremental.
    SyncImporter importer(m_db, m_repoDir);
    SyncImporter::Result res;
    if (m_oldHead.isEmpty()) {
        res = importer.importAll();
    } else {
        const auto diff = m_git->runBlocking(
            {QStringLiteral("diff"), QStringLiteral("--name-only"),
             m_oldHead + QStringLiteral("..HEAD"), QStringLiteral("--"), QStringLiteral("data")});
        const QStringList paths = diff.stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        res = importer.importPaths(paths);
    }
    if (!res.ok) {
        m_lastError = res.error.isEmpty() ? tr("Error al importar datos remotos.") : res.error;
        return false;
    }
    if (res.applied)
        emit remoteDataApplied();

    // Notas: la fusión dejó notes/ del repo con la unión resuelta; tráela al vault.
    processPendingCommands(); // command/*.passcmd traidos por la fusión
    mirrorNotesFromRepo();

    // Re-export: si el LWW conservó filas locales más nuevas, vuelve a publicarlas.
    if (!exportMirror())
        return false;
    commitAllBlockingIfDirty(tr("Pass: re-publicación tras fusión %1").arg(nowIso()));
    return true;
}

bool GitSyncService::resolveConflictsBlocking() {
    const auto unmerged = m_git->runBlocking(
        {QStringLiteral("diff"), QStringLiteral("--name-only"), QStringLiteral("--diff-filter=U")});
    const QStringList files = unmerged.stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (const QString& path : files) {
        const auto ours = m_git->runBlocking({QStringLiteral("show"), QStringLiteral(":2:") + path});
        const auto theirs =
            m_git->runBlocking({QStringLiteral("show"), QStringLiteral(":3:") + path});

        QString keep; // "--ours" o "--theirs"
        if (path.startsWith(QStringLiteral("data/"))) {
            // Gana el updated_at más nuevo; si falta un lado, conserva el presente.
            if (!ours.ok)
                keep = QStringLiteral("--theirs");
            else if (!theirs.ok)
                keep = QStringLiteral("--ours");
            else {
                const QDateTime tOurs = parseTimestamp(ours.stdOut.toUtf8());
                const QDateTime tTheirs = parseTimestamp(theirs.stdOut.toUtf8());
                keep = (tTheirs.isValid() && (!tOurs.isValid() || tTheirs > tOurs))
                           ? QStringLiteral("--theirs")
                           : QStringLiteral("--ours");
            }
        } else if (path.startsWith(QStringLiteral("notes/"))) {
            // Gana el remoto, pero la versión local se conserva como copia (cero pérdida).
            if (ours.ok) {
                const QFileInfo fi(m_repoDir + QLatin1Char('/') + path);
                const QString backup =
                    fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                    QStringLiteral(" (conflicto %1 %2).md")
                        .arg(m_deviceName, QDate::currentDate().toString(Qt::ISODate));
                QFile bf(backup);
                if (bf.open(QIODevice::WriteOnly)) {
                    bf.write(ours.stdOut.toUtf8());
                    bf.close();
                    m_git->runBlocking({QStringLiteral("add"), QStringLiteral("--"), backup});
                }
            }
            keep = theirs.ok ? QStringLiteral("--theirs") : QStringLiteral("--ours");
        } else {
            // presence/, manifest.json, etc.: gana lo local.
            keep = QStringLiteral("--ours");
        }

        const auto co =
            m_git->runBlocking({QStringLiteral("checkout"), keep, QStringLiteral("--"), path});
        if (!co.ok) {
            // El lado elegido no existe (modify/delete): resuelve por presencia.
            if (keep == QStringLiteral("--theirs") && !theirs.ok)
                m_git->runBlocking({QStringLiteral("rm"), QStringLiteral("--"), path});
        }
        m_git->runBlocking({QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--"), path});
    }

    // Completa la fusión con el mensaje por defecto.
    const auto commit = m_git->runBlocking(withIdentity({QStringLiteral("commit"),
                                                        QStringLiteral("--no-edit")}));
    return commit.ok;
}

void GitSyncService::doPush() {
    m_git->run(
        {QStringLiteral("push"), QStringLiteral("origin"),
         QStringLiteral("HEAD:") + m_branch},
        [this](const GitRunner::Result& r) {
            if (r.ok) {
                finishCycle(Status::Idle);
                return;
            }
            if (!r.started) {
                setStatus(Status::Disabled);
                failCycle(tr("Git no está disponible en el sistema."));
                return;
            }
            const QString out = r.stdErr + r.stdOut;
            const bool nonFf = out.contains(QStringLiteral("non-fast-forward")) ||
                               out.contains(QStringLiteral("fetch first")) ||
                               out.contains(QStringLiteral("rejected"));
            if (nonFf && m_pushRetries < 2) {
                ++m_pushRetries;
                doFetch(); // otro dispositivo pushó antes: re-fusiona y reintenta
                return;
            }
            m_lastError = tr("No se pudo enviar al remoto; quedó guardado en local.");
            finishCycle(Status::Warning);
        },
        kNetTimeoutMs);
}

void GitSyncService::finishCycle(Status finalStatus) {
    if (finalStatus == Status::Idle) {
        persistLastSync();
        m_lastError.clear();
    }
    m_inFlight = false;
    setStatus(finalStatus);
    if (finalStatus == Status::Warning && !m_lastError.isEmpty())
        emit errorOccurred(m_lastError);
    if (m_pendingSync) {
        m_pendingSync = false;
        QTimer::singleShot(0, this, &GitSyncService::syncNow);
    }
}

void GitSyncService::failCycle(const QString& message) {
    m_lastError = GitRunner::redacted(message);
    m_inFlight = false;
    setStatus(Status::Error);
    emit errorOccurred(m_lastError);
    if (m_pendingSync) {
        m_pendingSync = false;
        QTimer::singleShot(0, this, &GitSyncService::syncNow);
    }
}

// ---------------------------------------------------------------------------
// Presencia
// ---------------------------------------------------------------------------

bool GitSyncService::writePresence(bool open) {
    if (m_deviceId.isEmpty())
        return false;
    const QString dir = m_repoDir + QStringLiteral("/presence");
    if (!QDir().mkpath(dir))
        return false;
    QJsonObject o;
    o.insert(QStringLiteral("device_id"), m_deviceId);
    o.insert(QStringLiteral("device_name"), m_deviceName);
    o.insert(QStringLiteral("status"), open ? QStringLiteral("open") : QStringLiteral("closed"));
    o.insert(QStringLiteral("last_seen"), nowIso());
    QSaveFile f(dir + QLatin1Char('/') + m_deviceId + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

void GitSyncService::checkPresence() {
    if (m_presenceWarned)
        return;
    QDir dir(m_repoDir + QStringLiteral("/presence"));
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QFileInfo& fi : files) {
        if (fi.completeBaseName() == m_deviceId)
            continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.value(QStringLiteral("status")).toString() != QStringLiteral("open"))
            continue;
        QDateTime seen = QDateTime::fromString(o.value(QStringLiteral("last_seen")).toString(),
                                               Qt::ISODate);
        seen.setTimeZone(QTimeZone::utc());
        if (seen.isValid() && seen.secsTo(now) <= kPresenceFreshSecs) {
            m_presenceWarned = true;
            emit presenceWarning(o.value(QStringLiteral("device_name")).toString());
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Clonado / adopción / privacidad / cierre
// ---------------------------------------------------------------------------

void GitSyncService::cloneRepo(const QString& url, const QString& destDir) {
    if (!GitRunner::isAllowedRemoteUrl(url, m_git->allowLocalRemotes())) {
        emit cloneFinished(false, QString(), tr("La URL del repositorio no está permitida."));
        return;
    }
    const QString parent = QFileInfo(destDir).absolutePath();
    QDir().mkpath(parent);
    m_git->setRepoDir(parent); // el clone se ejecuta desde el directorio padre
    m_git->run(
        {QStringLiteral("clone"), url, destDir},
        [this, destDir](const GitRunner::Result& r) {
            if (!r.ok) {
                emit cloneFinished(false, QString(),
                                   r.started ? r.stdErr : tr("Git no está disponible."));
                return;
            }
            m_repoDir = destDir;
            m_git->setRepoDir(destDir);
            // Detecta la rama (incluso sin commits, con symbolic-ref).
            auto sym = m_git->runBlocking(
                {QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")});
            m_branch = sym.ok ? sym.stdOut.trimmed() : QString();
            if (m_branch.isEmpty())
                m_branch = QStringLiteral("main");
            m_needFullImport = true;

            // Andamiaje mínimo de un repo nuevo (idempotente si ya existe).
            QFile gitignore(destDir + QStringLiteral("/.gitignore"));
            if (!gitignore.exists() && gitignore.open(QIODevice::WriteOnly))
                gitignore.write(".obsidian/workspace*\n*.tmp\n");
            const QString notesDir = destDir + QStringLiteral("/notes");
            QDir().mkpath(notesDir);
            QFile keep(notesDir + QStringLiteral("/.gitkeep"));
            if (!keep.exists() && keep.open(QIODevice::WriteOnly))
                keep.write("");

            emit cloneFinished(true, m_branch, QString());
            syncNow();
        },
        GitRunner::kCloneTimeoutMs);
}

void GitSyncService::adoptExistingClone(const QString& dir) {
    m_git->setRepoDir(dir);
    const auto inside = m_git->runBlocking(
        {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")});
    if (!inside.ok || inside.stdOut.trimmed() != QStringLiteral("true")) {
        emit cloneFinished(false, QString(), tr("La carpeta no es un repositorio git."));
        return;
    }
    const auto remote = m_git->runBlocking(
        {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
    if (!remote.ok ||
        !GitRunner::isAllowedRemoteUrl(remote.stdOut.trimmed(), m_git->allowLocalRemotes())) {
        emit cloneFinished(false, QString(), tr("El remoto del repositorio no está permitido."));
        return;
    }
    auto sym = m_git->runBlocking(
        {QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")});
    m_branch = sym.ok && !sym.stdOut.trimmed().isEmpty() ? sym.stdOut.trimmed()
                                                         : QStringLiteral("main");
    m_repoDir = dir;
    m_needFullImport = true;
    emit cloneFinished(true, m_branch, QString());
    syncNow();
}

void GitSyncService::checkRepoIsPrivate(std::function<void(bool isPrivate)> cb) {
    const auto remote = m_git->runBlocking(
        {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
    const QString url = remote.stdOut.trimmed();
    if (!remote.ok || !GitRunner::isAllowedRemoteUrl(url, m_git->allowLocalRemotes())) {
        cb(true); // sin remoto válido: no podemos afirmar que sea público
        return;
    }
    // ls-remote SIN credenciales: si funciona, el repo es accesible sin auth => público.
    m_git->run(
        {QStringLiteral("-c"), QStringLiteral("credential.helper="),
         QStringLiteral("ls-remote"), url},
        [cb](const GitRunner::Result& r) { cb(/*isPrivate=*/!r.ok); }, kNetTimeoutMs);
}

void GitSyncService::shutdownSync(int budgetMs) {
    if (!isConfigured())
        return;
    exportMirror();
    writePresence(/*open=*/false);
    commitAllBlockingIfDirty(tr("Pass: cierre %1").arg(nowIso()));
    // Push final best-effort y acotado: si no llega, queda commiteado en local.
    m_git->runBlocking(
        {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("HEAD:") + m_branch},
        budgetMs);
}

} // namespace pass::sync
