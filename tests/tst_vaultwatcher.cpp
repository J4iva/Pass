// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/notes/VaultWatcher.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace pass;

namespace {

void writeFile(const QString& path, const QByteArray& content) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();
}

} // namespace

class VaultWatcherTest : public QObject {
    Q_OBJECT

private slots:
    void detectsNewFiles() {
        QTemporaryDir dir;
        VaultWatcher watcher;
        watcher.watch(dir.path());
        QSignalSpy vaultSpy(&watcher, &VaultWatcher::vaultChanged);

        writeFile(dir.path() + QStringLiteral("/nueva.md"), "# hola\n");
        QTRY_VERIFY_WITH_TIMEOUT(vaultSpy.count() >= 1, 5000);
    }

    void detectsExternalEdits() {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/nota.md");
        writeFile(path, "v1\n");

        VaultWatcher watcher;
        watcher.watch(dir.path());
        QSignalSpy noteSpy(&watcher, &VaultWatcher::noteChanged);

        QTest::qWait(100);
        writeFile(path, "v2 con cambios\n");
        QTRY_VERIFY_WITH_TIMEOUT(noteSpy.count() >= 1, 5000);
        QCOMPARE(noteSpy.takeFirst().at(0).toString(), QStringLiteral("nota.md"));
    }

    void stopSilencesSignals() {
        QTemporaryDir dir;
        VaultWatcher watcher;
        watcher.watch(dir.path());
        QSignalSpy vaultSpy(&watcher, &VaultWatcher::vaultChanged);

        watcher.stop();
        writeFile(dir.path() + QStringLiteral("/tarde.md"), "x\n");
        QTest::qWait(1200);
        QCOMPARE(vaultSpy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(VaultWatcherTest)
#include "tst_vaultwatcher.moc"
