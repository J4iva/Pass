// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/notes/NoteSerializer.h"

#include <QtTest>

using namespace pass;

class NoteSerializerTest : public QObject {
    Q_OBJECT

private slots:
    void parsesFrontmatterAndBody() {
        const QString content = QStringLiteral("---\ncreated: 2026-06-10\napp: pass\n---\n# Hola\n");
        const auto doc = NoteSerializer::parse(content);
        QVERIFY(doc.hasFrontmatter);
        QCOMPARE(doc.frontmatter.size(), 2);
        QCOMPARE(NoteSerializer::value(doc, QStringLiteral("created")),
                 QStringLiteral("2026-06-10"));
        QCOMPARE(doc.body, QStringLiteral("# Hola\n"));
    }

    void contentWithoutFrontmatterIsAllBody() {
        const auto doc = NoteSerializer::parse(QStringLiteral("# Solo cuerpo\n"));
        QVERIFY(!doc.hasFrontmatter);
        QCOMPARE(doc.body, QStringLiteral("# Solo cuerpo\n"));
    }

    void roundTripPreservesUnknownKeys() {
        // Claves añadidas por el usuario en Obsidian, incluso anidadas,
        // deben sobrevivir a un parse + setValue + serialize.
        const QString content = QStringLiteral(
            "---\ncreated: 2026-06-10\naliases:\n  - nota1\ncustom: valor\n---\ncuerpo");
        auto doc = NoteSerializer::parse(content);
        NoteSerializer::setValue(doc, QStringLiteral("created"), QStringLiteral("2026-06-11"));

        const QString out = NoteSerializer::serialize(doc);
        QVERIFY(out.contains(QStringLiteral("aliases:\n  - nota1")));
        QVERIFY(out.contains(QStringLiteral("custom: valor")));
        QVERIFY(out.contains(QStringLiteral("created: 2026-06-11")));
        QVERIFY(out.endsWith(QStringLiteral("---\ncuerpo")));
    }

    void setValueAddsMissingKey() {
        NoteSerializer::Document doc;
        NoteSerializer::setValue(doc, QStringLiteral("subject"), QStringLiteral("Álgebra"));
        QVERIFY(doc.hasFrontmatter);
        QCOMPARE(NoteSerializer::value(doc, QStringLiteral("subject")), QStringLiteral("Álgebra"));
    }

    void exactRoundTripWithoutChanges() {
        const QString content = QStringLiteral("---\na: 1\nb: 2\n---\ncuerpo\nfinal\n");
        QCOMPARE(NoteSerializer::serialize(NoteSerializer::parse(content)), content);
    }
};

QTEST_APPLESS_MAIN(NoteSerializerTest)
#include "tst_noteserializer.moc"
