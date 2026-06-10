// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/notes/VaultService.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace pass;

class VaultServiceTest : public QObject {
    Q_OBJECT

private slots:
    void createListReadWrite() {
        QTemporaryDir vault;
        VaultService service(vault.path(), QStringLiteral("Pass"));
        QVERIFY(service.vaultExists());

        const auto fileName = service.createNote(QStringLiteral("Integrales por partes"),
                                                 QStringLiteral("Cálculo"));
        QVERIFY(fileName.has_value());
        QVERIFY(fileName->endsWith(QStringLiteral("Integrales por partes.md")));

        const auto notes = service.notes();
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes[0].title, QStringLiteral("Integrales por partes"));

        auto content = service.readNote(*fileName);
        QVERIFY(content.has_value());
        QVERIFY(content->startsWith(QStringLiteral("---\n")));
        QVERIFY(content->contains(QStringLiteral("app: pass")));
        QVERIFY(content->contains(QStringLiteral("subject: Cálculo")));

        QVERIFY(service.writeNote(*fileName, *content + QStringLiteral("más texto\n")));
        QVERIFY(service.readNote(*fileName)->endsWith(QStringLiteral("más texto\n")));
    }

    void collisionsGetSuffix() {
        QTemporaryDir vault;
        VaultService service(vault.path(), QString());
        const auto first = service.createNote(QStringLiteral("Duplicada"));
        const auto second = service.createNote(QStringLiteral("Duplicada"));
        QVERIFY(first.has_value() && second.has_value());
        QVERIFY(*first != *second);
        QVERIFY(second->contains(QStringLiteral("(2)")));
    }

    void sanitizesForbiddenCharacters() {
        QCOMPARE(VaultService::sanitizeTitle(QStringLiteral("a/b\\c:d*e?f\"g<h>i|j#k")),
                 QStringLiteral("abcdefghijk"));
        QVERIFY(VaultService::sanitizeTitle(QString(120, QLatin1Char('x'))).size() <= 80);
    }

    void missingVaultFailsGracefully() {
        VaultService service(QStringLiteral("C:/ruta/que/no/existe"), QStringLiteral("Pass"));
        QVERIFY(!service.vaultExists());
        QVERIFY(!service.createNote(QStringLiteral("Nota")).has_value());
        QVERIFY(!service.writeNote(QStringLiteral("x.md"), QStringLiteral("y")));
        QVERIFY(service.notes().isEmpty());
    }
};

QTEST_GUILESS_MAIN(VaultServiceTest)
#include "tst_vaultservice.moc"
