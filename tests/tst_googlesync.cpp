// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/calendar/CalendarService.h"
#include "pass/db/Database.h"
#include "pass/google/CalendarClient.h"
#include "pass/google/GoogleAuthService.h"
#include "pass/google/GoogleSyncService.h"
#include "pass/google/TokenStore.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SyncStateRepository.h"

#include <QSignalSpy>
#include <QTimeZone>
#include <QtTest>

using namespace pass;

namespace {

// TokenStore en memoria: evita tocar el Administrador de credenciales real.
class InMemoryTokenStore : public TokenStore {
public:
    std::optional<QString> read(const QString& key) const override {
        const auto it = m_data.find(key);
        return it == m_data.end() ? std::nullopt : std::optional<QString>(it.value());
    }
    bool write(const QString& key, const QString& value) override {
        m_data[key] = value;
        return true;
    }
    bool remove(const QString& key) override {
        m_data.remove(key);
        return true;
    }

private:
    QHash<QString, QString> m_data;
};

// Cliente de calendario simulado: respuestas programadas y registro de llamadas.
// No necesita Q_OBJECT (no añade señales/slots propios).
class FakeCalendarClient : public CalendarClient {
public:
    QList<QPair<ListPage, ApiError>> listResponses;
    QStringList receivedSyncTokens;
    QStringList receivedPageTokens;

    ApiError insertError;
    ApiError patchError;
    ApiError deleteError;
    RemoteEvent insertResult;
    RemoteEvent patchResult;
    int insertCalls = 0;
    int patchCalls = 0;
    int deleteCalls = 0;

    void listEvents(const QString& syncToken, const QString& pageToken, const QDateTime&,
                    const QDateTime&, ListCallback cb) override {
        receivedSyncTokens << syncToken;
        receivedPageTokens << pageToken;
        if (listResponses.isEmpty()) {
            cb({}, {});
            return;
        }
        const auto r = listResponses.takeFirst();
        cb(r.first, r.second);
    }
    void insertEvent(const CalendarEvent&, EventCallback cb) override {
        ++insertCalls;
        cb(insertResult, insertError);
    }
    void patchEvent(const CalendarEvent&, EventCallback cb) override {
        ++patchCalls;
        cb(patchResult, patchError);
    }
    void deleteEvent(const QString&, const QString&, PlainCallback cb) override {
        ++deleteCalls;
        cb(deleteError);
    }
};

RemoteEvent makeRemote(const QString& extId, const QString& title, const QString& etag,
                       bool cancelled = false) {
    RemoteEvent re;
    re.cancelled = cancelled;
    re.event.provider = QStringLiteral("google");
    re.event.externalId = extId;
    re.event.title = title;
    re.event.etag = etag;
    re.event.startUtc = QDateTime(QDate(2026, 6, 10), QTime(9, 0), QTimeZone::utc());
    re.event.endUtc = QDateTime(QDate(2026, 6, 10), QTime(10, 0), QTimeZone::utc());
    re.event.updatedAt = QDateTime(QDate(2026, 6, 1), QTime(0, 0), QTimeZone::utc());
    return re;
}

ListPage pageWith(const QList<RemoteEvent>& events, const QString& nextPage,
                  const QString& nextSync) {
    ListPage p;
    p.events = events;
    p.nextPageToken = nextPage;
    p.nextSyncToken = nextSync;
    return p;
}

} // namespace

class GoogleSyncTest : public QObject {
    Q_OBJECT

private:
    QString syncToken(const Database& db) {
        return SyncStateRepository(db.handle())
            .get(QString::fromLatin1(SyncStateRepository::kSyncToken))
            .value_or(QString());
    }

private slots:
    void fullSyncWithPagination() {
        Database db(QStringLiteral(":memory:"));
        InMemoryTokenStore store;
        store.write(TokenStore::kRefreshToken, QStringLiteral("r")); // isConnected()
        GoogleAuthService auth(store);
        FakeCalendarClient client;
        GoogleSyncService sync(db.handle(), client, auth);

        // Dos páginas; el syncToken final llega en la última.
        client.listResponses << qMakePair(pageWith({makeRemote("A", "Clase A", "e1")},
                                                   QStringLiteral("page2"), QString()),
                                          ApiError{})
                             << qMakePair(pageWith({makeRemote("B", "Clase B", "e2")}, QString(),
                                                   QStringLiteral("TOK1")),
                                          ApiError{});

        QSignalSpy finished(&sync, &GoogleSyncService::syncFinished);
        sync.syncNow();

        QCOMPARE(finished.count(), 1);
        // Primera llamada full (syncToken vacío), segunda con pageToken.
        QCOMPARE(client.receivedSyncTokens, QStringList({QString(), QString()}));
        QCOMPARE(client.receivedPageTokens, QStringList({QString(), QStringLiteral("page2")}));
        QCOMPARE(syncToken(db), QStringLiteral("TOK1"));

        EventRepository repo(db.handle());
        QVERIFY(repo.byExternalId(QStringLiteral("google"), QStringLiteral("A")).has_value());
        QVERIFY(repo.byExternalId(QStringLiteral("google"), QStringLiteral("B")).has_value());
        QCOMPARE(sync.status(), GoogleSyncService::Status::Idle);
    }

    void incrementalUsesSyncTokenAndDeletesCancelled() {
        Database db(QStringLiteral(":memory:"));
        InMemoryTokenStore store;
        store.write(TokenStore::kRefreshToken, QStringLiteral("r"));
        GoogleAuthService auth(store);
        FakeCalendarClient client;
        GoogleSyncService sync(db.handle(), client, auth);

        // Full inicial: inserta X con token TOK1.
        client.listResponses << qMakePair(
            pageWith({makeRemote("X", "Evento X", "e1")}, QString(), QStringLiteral("TOK1")),
            ApiError{});
        sync.syncNow();
        EventRepository repo(db.handle());
        QVERIFY(repo.byExternalId(QStringLiteral("google"), QStringLiteral("X")).has_value());

        // Incremental: X cancelado → se borra la fila espejo; nuevo token TOK2.
        client.receivedSyncTokens.clear();
        client.listResponses << qMakePair(
            pageWith({makeRemote("X", "Evento X", "e1", /*cancelled=*/true)}, QString(),
                     QStringLiteral("TOK2")),
            ApiError{});
        sync.syncNow();

        QCOMPARE(client.receivedSyncTokens, QStringList({QStringLiteral("TOK1")}));
        QVERIFY(!repo.byExternalId(QStringLiteral("google"), QStringLiteral("X")).has_value());
        QCOMPARE(syncToken(db), QStringLiteral("TOK2"));
    }

    void goneSyncTokenTriggersFullResync() {
        Database db(QStringLiteral(":memory:"));
        InMemoryTokenStore store;
        store.write(TokenStore::kRefreshToken, QStringLiteral("r"));
        GoogleAuthService auth(store);
        FakeCalendarClient client;
        GoogleSyncService sync(db.handle(), client, auth);

        // Sembramos un syncToken viejo.
        SyncStateRepository(db.handle())
            .set(QString::fromLatin1(SyncStateRepository::kSyncToken), QStringLiteral("OLD"));

        // 1ª llamada (incremental con OLD) → 410; 2ª (full) → ok con TOK2.
        client.listResponses << qMakePair(ListPage{}, ApiError{ApiError::GoneSyncToken, {}})
                             << qMakePair(pageWith({makeRemote("Z", "Z", "e9")}, QString(),
                                                   QStringLiteral("TOK2")),
                                          ApiError{});

        sync.syncNow();

        QCOMPARE(client.receivedSyncTokens, QStringList({QStringLiteral("OLD"), QString()}));
        QCOMPARE(syncToken(db), QStringLiteral("TOK2"));
        EventRepository repo(db.handle());
        QVERIFY(repo.byExternalId(QStringLiteral("google"), QStringLiteral("Z")).has_value());
    }

    void pushCreateStoresMirrorRow() {
        Database db(QStringLiteral(":memory:"));
        InMemoryTokenStore store;
        store.write(TokenStore::kRefreshToken, QStringLiteral("r"));
        GoogleAuthService auth(store);
        FakeCalendarClient client;
        GoogleSyncService sync(db.handle(), client, auth);
        CalendarService service(db.handle(), &sync);

        // Google devuelve el evento creado con id y etag.
        client.insertResult = makeRemote("NEW1", "Examen", "etag-new");

        CalendarEvent e;
        e.title = QStringLiteral("Examen");
        e.provider = QStringLiteral("google"); // checkbox "crear también en Google"
        e.startUtc = QDateTime(QDate(2026, 6, 12), QTime(9, 0), QTimeZone::utc());
        e.endUtc = QDateTime(QDate(2026, 6, 12), QTime(11, 0), QTimeZone::utc());

        QSignalSpy changed(&service, &CalendarProvider::eventsChanged);
        QVERIFY(service.addEvent(e));
        QCOMPARE(client.insertCalls, 1);
        QCOMPARE(changed.count(), 1);

        EventRepository repo(db.handle());
        const auto stored = repo.byExternalId(QStringLiteral("google"), QStringLiteral("NEW1"));
        QVERIFY(stored.has_value());
        QCOMPARE(stored->etag, QStringLiteral("etag-new"));
        QCOMPARE(stored->title, QStringLiteral("Examen"));
    }

    void pushCreateFailureRevertsLocally() {
        Database db(QStringLiteral(":memory:"));
        InMemoryTokenStore store;
        store.write(TokenStore::kRefreshToken, QStringLiteral("r"));
        GoogleAuthService auth(store);
        FakeCalendarClient client;
        GoogleSyncService sync(db.handle(), client, auth);
        CalendarService service(db.handle(), &sync);

        client.insertError = {ApiError::Network, QStringLiteral("offline")};

        CalendarEvent e;
        e.title = QStringLiteral("No subido");
        e.provider = QStringLiteral("google");
        e.startUtc = QDateTime(QDate(2026, 6, 12), QTime(9, 0), QTimeZone::utc());
        e.endUtc = QDateTime(QDate(2026, 6, 12), QTime(10, 0), QTimeZone::utc());

        QSignalSpy errors(&service, &CalendarService::syncErrorOccurred);
        QVERIFY(!service.addEvent(e));
        QCOMPARE(errors.count(), 1);
        // No se guardó nada en local (revert).
        EventRepository repo(db.handle());
        const auto from = QDateTime(QDate(2026, 6, 1), QTime(0, 0), QTimeZone::utc());
        const auto to = QDateTime(QDate(2026, 7, 1), QTime(0, 0), QTimeZone::utc());
        QCOMPARE(repo.between(from, to).size(), 0);
    }

    void updateConflict412KeepsGoogleVersion() {
        Database db(QStringLiteral(":memory:"));
        InMemoryTokenStore store;
        store.write(TokenStore::kRefreshToken, QStringLiteral("r"));
        GoogleAuthService auth(store);
        FakeCalendarClient client;
        GoogleSyncService sync(db.handle(), client, auth);
        CalendarService service(db.handle(), &sync);

        // Sembramos una fila espejo de Google ya existente.
        EventRepository repo(db.handle());
        CalendarEvent g = makeRemote("G1", "Original", "etag-1").event;
        g.id = QUuid::createUuid();
        QVERIFY(repo.upsertByExternalId(g));

        // El patch devuelve 412 (etag desfasado); re-pull devuelve la versión de Google.
        client.patchError = {ApiError::Conflict412, QStringLiteral("etag mismatch")};
        client.listResponses << qMakePair(
            pageWith({makeRemote("G1", "Version de Google", "etag-2")}, QString(),
                     QStringLiteral("TOKp")),
            ApiError{});

        CalendarEvent edit = g;
        edit.title = QStringLiteral("Mi cambio local");
        QSignalSpy errors(&service, &CalendarService::syncErrorOccurred);

        QVERIFY(!service.updateEvent(edit)); // el cambio local se descarta
        QCOMPARE(errors.count(), 1);
        QCOMPARE(client.patchCalls, 1);

        // Tras el re-pull, gana la versión de Google.
        const auto stored = repo.byExternalId(QStringLiteral("google"), QStringLiteral("G1"));
        QVERIFY(stored.has_value());
        QCOMPARE(stored->title, QStringLiteral("Version de Google"));
        QCOMPARE(stored->etag, QStringLiteral("etag-2"));
    }
};

QTEST_GUILESS_MAIN(GoogleSyncTest)
#include "tst_googlesync.moc"
