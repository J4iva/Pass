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
        QCOMPARE(notes[0].title, QStringLiteral("Cálculo - Integrales por partes"));

        auto content = service.readNote(*fileName);
        QVERIFY(content.has_value());
        QVERIFY(content->startsWith(QStringLiteral("---\n")));
        QVERIFY(content->contains(QStringLiteral("app: pass")));
        QVERIFY(content->contains(QStringLiteral("subject: Cálculo")));
        QVERIFY(content->contains(QStringLiteral("created:")));

        QVERIFY(service.writeNote(*fileName, *content + QStringLiteral("más texto\n")));
        QVERIFY(service.readNote(*fileName)->endsWith(QStringLiteral("más texto\n")));
    }

    void subjectNotesGetStudyTemplate() {
        QTemporaryDir vault;
        VaultService service(vault.path(), QString());
        const auto fileName = service.createNote(QStringLiteral("Tema 4"),
                                                 QStringLiteral("Física Cuántica"));
        QVERIFY(fileName.has_value());
        QVERIFY(fileName->contains(QStringLiteral("Física Cuántica - Tema 4")));

        const auto content = service.readNote(*fileName);
        QVERIFY(content->contains(QStringLiteral("Asignatura: Física Cuántica")));
        QVERIFY(content->contains(QStringLiteral("Creada:")));
        QVERIFY(content->contains(QStringLiteral("## Apuntes")));
        QVERIFY(content->contains(QStringLiteral("## Dudas")));
        QVERIFY(content->contains(QStringLiteral("tags: [pass, física-cuántica]")));
    }

    void freeNotesGetSimpleStructure() {
        QTemporaryDir vault;
        VaultService service(vault.path(), QString());
        const auto fileName = service.createNote(QStringLiteral("Ideas sueltas"));
        QVERIFY(fileName.has_value());

        const auto content = service.readNote(*fileName);
        QVERIFY(content->contains(QStringLiteral("# Ideas sueltas")));
        QVERIFY(content->contains(QStringLiteral("Creada:")));
        QVERIFY(!content->contains(QStringLiteral("## Apuntes")));
        QVERIFY(!content->contains(QStringLiteral("subject:")));
    }

    void deleteRemovesNote() {
        QTemporaryDir vault;
        VaultService service(vault.path(), QString());
        const auto fileName = service.createNote(QStringLiteral("Efímera"));
        QVERIFY(fileName.has_value());
        QCOMPARE(service.notes().size(), 1);

        QVERIFY(service.deleteNote(*fileName));
        QCOMPARE(service.notes().size(), 0);
        QVERIFY(!service.deleteNote(*fileName)); // ya no existe
        QVERIFY(!service.deleteNote(QString()));
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
