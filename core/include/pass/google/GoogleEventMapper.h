// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"

#include <QJsonObject>

namespace pass::GoogleEventMapper {

// Convierte un recurso "Event" de la API de Google a CalendarEvent.
//   - all-day: usa el campo `date` (fin exclusivo, igual que el convenio de Pass).
//   - con hora: usa `dateTime` (RFC3339) normalizado a UTC.
//   - si `cancelled` no es nulo, se pone a true cuando status == "cancelled"
//     (evento borrado en Google: el sync eliminará la fila espejo).
// El provider del resultado queda fijado a "google".
CalendarEvent fromJson(const QJsonObject& obj, bool* cancelled = nullptr);

// Construye el cuerpo JSON para crear/actualizar el evento en Google.
// Solo incluye los campos que Pass gestiona (summary, description, start, end).
QJsonObject toJson(const CalendarEvent& event);

} // namespace pass::GoogleEventMapper
