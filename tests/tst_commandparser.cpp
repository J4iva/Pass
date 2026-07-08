// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/command/CommandId.h"
#include "pass/command/CommandParser.h"

#include <QtTest>

using namespace pass::command;

class CommandParserTest : public QObject {
    Q_OBJECT

private slots:
    void parsesCreateSubjectWithColor() {
        const auto r =
            parse(QStringLiteral("Pass create subject Cálculo --color \"#3478f6\""));
        QVERIFY(r.ok);
        QCOMPARE(r.command.action, Action::Create);
        QCOMPARE(r.command.entity, Entity::Subject);
        QCOMPARE(r.command.positional, QStringList{QStringLiteral("Cálculo")});
        QCOMPARE(r.command.flags.value(QStringLiteral("color")), QStringLiteral("#3478f6"));
        QVERIFY(r.command.boolFlags.isEmpty());
    }

    void parsesCreateTaskWithQuotedTitle() {
        const auto r = parse(QStringLiteral(
            "Pass create task \"Entregar práctica 3\" --due 2026-06-20T22:00:00Z --subject Cálculo"));
        QVERIFY(r.ok);
        QCOMPARE(r.command.entity, Entity::Task);
        QCOMPARE(r.command.positional, QStringList{QStringLiteral("Entregar práctica 3")});
        QCOMPARE(r.command.flags.value(QStringLiteral("due")),
                 QStringLiteral("2026-06-20T22:00:00Z"));
        QCOMPARE(r.command.flags.value(QStringLiteral("subject")), QStringLiteral("Cálculo"));
    }

    void parsesCreateEventWithBoolFlag() {
        const auto r = parse(QStringLiteral(
            "Pass create event \"Día sin clases\" --start 2026-06-20 --all-day --end 2026-06-20"));
        QVERIFY(r.ok);
        QCOMPARE(r.command.entity, Entity::Event);
        QVERIFY(r.command.boolFlags.contains(QStringLiteral("all-day")));
        QCOMPARE(r.command.flags.value(QStringLiteral("start")), QStringLiteral("2026-06-20"));
    }

    void parsesFlagInlineEquals() {
        const auto r = parse(QStringLiteral("Pass create note Integrales --subject=Cálculo"));
        QVERIFY(r.ok);
        QCOMPARE(r.command.flags.value(QStringLiteral("subject")), QStringLiteral("Cálculo"));
        QCOMPARE(r.command.positional, QStringList{QStringLiteral("Integrales")});
    }

    void parsesCreateSessionAllFlags() {
        const auto r = parse(QStringLiteral(
            "Pass create session --start 2026-06-14T16:00:00Z --minutes 50 --subject Cálculo "
            "--topic \"Integrales por partes\""));
        QVERIFY(r.ok);
        QCOMPARE(r.command.entity, Entity::Session);
        QVERIFY(r.command.positional.isEmpty());
        QCOMPARE(r.command.flags.value(QStringLiteral("minutes")), QStringLiteral("50"));
        QCOMPARE(r.command.flags.value(QStringLiteral("topic")),
                 QStringLiteral("Integrales por partes"));
    }

    void bodyWithEscapedQuotes() {
        // El usuario escribe: Pass create note X --body "a\"b"  -> el tokenizador
        // consume el backslash de escape y deja la comilla: body = a"b
        const auto r = parse(QStringLiteral("Pass create note X --body \"a\\\"b\""));
        QVERIFY(r.ok);
        QCOMPARE(r.command.flags.value(QStringLiteral("body")), QStringLiteral("a\"b"));
    }

    void bodyWithEscapedBackslash() {
        // El usuario escribe: Pass create note X --body "a\\b"  -> body = a\b
        const auto r = parse(QStringLiteral("Pass create note X --body \"a\\\\b\""));
        QVERIFY(r.ok);
        QCOMPARE(r.command.flags.value(QStringLiteral("body")), QStringLiteral("a\\b"));
    }

    void rejectsUnknownAction() {
        QVERIFY(!parse(QStringLiteral("Pass delete subject X")).ok);
    }

    void rejectsUnknownEntity() {
        const auto r = parse(QStringLiteral("Pass create blah X"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("entidad")));
    }

    void rejectsUnknownFlag() {
        const auto r = parse(QStringLiteral("Pass create subject X --nope 1"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("flag desconocido")));
    }

    void rejectsBoolFlagWithValue() {
        const auto r =
            parse(QStringLiteral("Pass create event X --start 2026-06-20 --all-day=true"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("all-day")));
    }

    void rejectsValueFlagWithoutValue() {
        const auto r = parse(QStringLiteral("Pass create subject X --color"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("color")));
    }

    void rejectsUnterminatedQuote() {
        const auto r = parse(QStringLiteral("Pass create note \"sin cerrar"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("comillas")));
    }

    void rejectsTooFewTokens() {
        QVERIFY(!parse(QStringLiteral("Pass create")).ok);
        QVERIFY(!parse(QString()).ok);
    }

    void sourceFileIsStoredForTraces() {
        const auto r =
            parse(QStringLiteral("Pass create subject X"), QStringLiteral("001.passcmd"));
        QVERIFY(r.ok);
        QCOMPARE(r.command.sourceFile, QStringLiteral("001.passcmd"));
    }

    void deterministicIdIsStableAcrossSameText() {
        const Command a =
            parse(QStringLiteral("Pass create subject Cálculo"), QStringLiteral("x")).command;
        const Command b =
            parse(QStringLiteral("Pass create subject Cálculo"), QStringLiteral("y")).command;
        QVERIFY(deterministicId(a) == deterministicId(b));
        QVERIFY(!deterministicId(a).isNull());
    }

    void deterministicIdChangesWithDifferentText() {
        const Command a = parse(QStringLiteral("Pass create subject Cálculo")).command;
        const Command b = parse(QStringLiteral("Pass create subject Álgebra")).command;
        QVERIFY(deterministicId(a) != deterministicId(b));
    }

    void deterministicIdIgnoresLineEndingsAndTrailingSpace() {
        const Command a = parse(QStringLiteral("Pass create subject Cálculo")).command;
        const Command b = parse(QStringLiteral("Pass create subject Cálculo\r\n  ")).command;
        QVERIFY(deterministicId(a) == deterministicId(b));
    }

    void deterministicIdIsCaseSensitive() {
        // "Cálculo" != "cálculo": la canonicalización NO lower-casa.
        const Command a = parse(QStringLiteral("Pass create subject Cálculo")).command;
        const Command b = parse(QStringLiteral("Pass create subject cálculo")).command;
        QVERIFY(deterministicId(a) != deterministicId(b));
    }
};

QTEST_GUILESS_MAIN(CommandParserTest)
#include "tst_commandparser.moc"
