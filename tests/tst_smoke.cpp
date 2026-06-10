// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/Version.h"

#include <QtTest>

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void versionIsNotEmpty() { QVERIFY(!pass::appVersion().isEmpty()); }
};

QTEST_APPLESS_MAIN(SmokeTest)
#include "tst_smoke.moc"
