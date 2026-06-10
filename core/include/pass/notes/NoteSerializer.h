// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace pass {

// Parser/generador mínimo de frontmatter YAML para notas Markdown.
//
// Solo entiende líneas "clave: valor" planas, que es lo que la app escribe.
// Todo lo que el usuario añada en Obsidian (claves desconocidas, listas,
// anidados) se conserva como texto crudo en el round-trip.
class NoteSerializer {
public:
    struct Document {
        QStringList frontmatter; // líneas crudas entre los dos "---" (sin incluirlos)
        QString body;
        bool hasFrontmatter = false;
    };

    static Document parse(const QString& content);
    static QString serialize(const Document& doc);

    // Valor de una línea "key: value" del frontmatter (vacío si no está).
    static QString value(const Document& doc, const QString& key);
    // Actualiza la línea "key: ..." conservando su posición, o la añade al final.
    static void setValue(Document& doc, const QString& key, const QString& value);
};

} // namespace pass
