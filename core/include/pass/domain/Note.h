// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>

namespace pass {

// Metadatos de una nota. El contenido vive en el fichero .md del vault de
// Obsidian: el sistema de ficheros es la fuente de verdad, no la base de datos.
struct Note {
    QString fileName; // nombre del .md dentro de la carpeta de notas (id de la nota)
    QString title;    // nombre sin la parte de fecha ni la extensión
    QDateTime modified;
};

} // namespace pass
