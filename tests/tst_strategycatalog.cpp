// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/session/StrategyCatalog.h"

#include <QtTest>

using namespace pass;

namespace {

PomodoroStrategy strategy(const QString& name, int work, int brk, int longBrk, int cycles) {
    return {QUuid::createUuid(), name, work, brk, longBrk, cycles, true};
}

} // namespace

class StrategyCatalogTest : public QObject {
    Q_OBJECT

private slots:
    void proposesMaxCyclesThatFit() {
        // 120 min con 25/5 (largo 15 cada 4): 25+5+25+5+25+5+25 = 115 → 4 ciclos
        const auto plans = StrategyCatalog::proposals(
            120, {strategy(QStringLiteral("25/5"), 25, 5, 15, 4)});
        QCOMPARE(plans.size(), 1);
        QCOMPARE(plans[0].cycles, 4);
        QCOMPARE(plans[0].totalWorkMinutes, 100);
        QCOMPARE(plans[0].totalMinutes, 115);
    }

    void countsLongBreaksBetweenCycles() {
        // 50/10 con largo 25 cada 2: 50+10+50 = 110 caben en 120;
        // el siguiente bloque exigiría 25+50 más → no cabe.
        const auto plans = StrategyCatalog::proposals(
            120, {strategy(QStringLiteral("50/10"), 50, 10, 25, 2)});
        QCOMPARE(plans.size(), 1);
        QCOMPARE(plans[0].cycles, 2);
        QCOMPARE(plans[0].totalMinutes, 110);
    }

    void skipsStrategiesThatDoNotFit() {
        const auto plans = StrategyCatalog::proposals(
            30, {strategy(QStringLiteral("45/5"), 45, 5, 20, 3)});
        QVERIFY(plans.isEmpty());
    }

    void sortsByEffectiveWorkDescending() {
        const auto plans = StrategyCatalog::proposals(
            60, {strategy(QStringLiteral("25/5"), 25, 5, 15, 4),
                 strategy(QStringLiteral("50/10"), 50, 10, 25, 2)});
        QCOMPARE(plans.size(), 2);
        QVERIFY(plans[0].totalWorkMinutes >= plans[1].totalWorkMinutes);
    }

    void negativeBreakDoesNotHang() {
        // Regresión H-1 (CWE-835): un descanso negativo de una estrategia
        // corrupta/sincronizada hacía `next <= 0`, dejaba de crecer `total` y el
        // bucle no terminaba (congelaba la GUI). Debe devolver un plan finito.
        const auto plans = StrategyCatalog::proposals(
            120, {strategy(QStringLiteral("corrupta"), 25, -100, -100, 4)});
        QCOMPARE(plans.size(), 1);
        QCOMPARE(plans[0].cycles, 1); // no progresa más allá del primer bloque
        QCOMPARE(plans[0].totalMinutes, 25);
    }

    void longBreakNegativeDoesNotHang() {
        // Igual que el anterior pero por el descanso largo (otra vía de `next<=0`).
        const auto plans = StrategyCatalog::proposals(
            600, {strategy(QStringLiteral("corrupta2"), 10, 5, -9999, 2)});
        QCOMPARE(plans.size(), 1);
        // 10 +5+10 = 25 (2 ciclos); el 3er bloque exigiría el descanso largo
        // negativo => next<=0 => deja de progresar. Plan finito.
        QCOMPARE(plans[0].cycles, 2);
        QCOMPARE(plans[0].totalMinutes, 25);
    }

    void describeIsHumanReadable() {
        const auto plans = StrategyCatalog::proposals(
            120, {strategy(QStringLiteral("25/5"), 25, 5, 15, 4)});
        const QString text = StrategyCatalog::describe(plans[0]);
        QVERIFY(text.contains(QStringLiteral("4×25")));
        QVERIFY(text.contains(QStringLiteral("100")));
    }
};

QTEST_APPLESS_MAIN(StrategyCatalogTest)
#include "tst_strategycatalog.moc"
