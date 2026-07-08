// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/DataChangeNotifier.h"

namespace pass {

DataChangeNotifier::DataChangeNotifier(QObject* parent) : QObject(parent) {}

DataChangeNotifier& DataChangeNotifier::instance() {
    static DataChangeNotifier s_instance;
    return s_instance;
}

void DataChangeNotifier::notifyChanged() {
    emit changed();
}

} // namespace pass
