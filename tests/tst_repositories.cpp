// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Database.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"

#include <QtTest>

using namespace pass;

class RepositoriesTest : public QObject {
    Q_OBJECT

private slots:
    void subjectCrud() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository repo(db.handle());

        Subject algebra{QUuid::createUuid(), QStringLiteral("Álgebra"), QStringLiteral("#3366ff"),
                        false};
        QVERIFY(repo.add(algebra));
        QCOMPARE(repo.all().size(), 1);

        auto fetched = repo.byId(algebra.id);
        QVERIFY(fetched.has_value());
        QCOMPARE(fetched->name, algebra.name);
        QCOMPARE(fetched->colorHex, algebra.colorHex);

        fetched->name = QStringLiteral("Álgebra Lineal");
        fetched->archived = true;
        QVERIFY(repo.update(*fetched));
        QCOMPARE(repo.all(/*includeArchived=*/false).size(), 0);
        QCOMPARE(repo.all(/*includeArchived=*/true).size(), 1);

        QVERIFY(repo.remove(algebra.id));
        QCOMPARE(repo.all(true).size(), 0);
    }

    void subjectNameIsUnique() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository repo(db.handle());
        QVERIFY(repo.add({QUuid::createUuid(), QStringLiteral("Física"), {}, false}));
        QVERIFY(!repo.add({QUuid::createUuid(), QStringLiteral("Física"), {}, false}));
    }

    void builtinStrategiesAreSeeded() {
        Database db(QStringLiteral(":memory:"));
        StrategyRepository repo(db.handle());

        const auto strategies = repo.all();
        QCOMPARE(strategies.size(), 3);
        for (const auto& s : strategies)
            QVERIFY(s.builtin);
        QCOMPARE(strategies[0].workMinutes, 25); // orden: builtin primero, por work_min
        QCOMPARE(strategies[1].workMinutes, 45);
        QCOMPARE(strategies[2].workMinutes, 50);
    }

    void customStrategyCrudAndBuiltinProtection() {
        Database db(QStringLiteral(":memory:"));
        StrategyRepository repo(db.handle());

        PomodoroStrategy custom{QUuid::createUuid(), QStringLiteral("Maratón 90/15"), 90, 15, 30,
                                2, false};
        QVERIFY(repo.add(custom));
        QCOMPARE(repo.all().size(), 4);

        auto fetched = repo.byId(custom.id);
        QVERIFY(fetched.has_value());
        QCOMPARE(fetched->workMinutes, 90);

        // Las builtin no se pueden borrar; las personalizadas sí.
        const auto builtinId = repo.all().first().id;
        QVERIFY(!repo.remove(builtinId));
        QVERIFY(repo.remove(custom.id));
        QCOMPARE(repo.all().size(), 3);
    }
};

QTEST_GUILESS_MAIN(RepositoriesTest)
#include "tst_repositories.moc"
