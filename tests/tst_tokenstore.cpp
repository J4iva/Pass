// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/WinCredTokenStore.h"

#include <QUuid>
#include <QtTest>

using namespace pass;

// Verifica el almacén sobre el Administrador de credenciales de Windows.
// Usa un prefijo único por ejecución ("PassTest-<uuid>") para no tocar ni dejar
// residuos en las credenciales reales de la app.
class TokenStoreTest : public QObject {
    Q_OBJECT

private:
    QString m_prefix;

private slots:
    void initTestCase() {
        m_prefix = QStringLiteral("PassTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    void writeReadRemoveRoundtrip() {
        WinCredTokenStore store(m_prefix);
        const QString key = QStringLiteral("google/access_token");

        QVERIFY(!store.read(key).has_value());

        QVERIFY(store.write(key, QStringLiteral("ya29.secret-token-áéí")));
        const auto got = store.read(key);
        QVERIFY(got.has_value());
        QCOMPARE(got.value(), QStringLiteral("ya29.secret-token-áéí"));

        // Sobrescribir actualiza el valor.
        QVERIFY(store.write(key, QStringLiteral("ya29.rotated")));
        QCOMPARE(store.read(key).value(), QStringLiteral("ya29.rotated"));

        QVERIFY(store.remove(key));
        QVERIFY(!store.read(key).has_value());
        // Borrar algo inexistente no es error.
        QVERIFY(store.remove(key));
    }

    void keysAreIsolated() {
        WinCredTokenStore store(m_prefix);
        QVERIFY(store.write(TokenStore::kClientId, QStringLiteral("client-123")));
        QVERIFY(store.write(TokenStore::kRefreshToken, QStringLiteral("refresh-xyz")));
        QCOMPARE(store.read(TokenStore::kClientId).value(), QStringLiteral("client-123"));
        QCOMPARE(store.read(TokenStore::kRefreshToken).value(), QStringLiteral("refresh-xyz"));
        QVERIFY(store.remove(TokenStore::kClientId));
        QVERIFY(store.remove(TokenStore::kRefreshToken));
    }
};

QTEST_GUILESS_MAIN(TokenStoreTest)
#include "tst_tokenstore.moc"
