#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

namespace still::credential_fixture {
extern QMap<QString, QByteArray> keys;
extern QString error;
void reset();
} // namespace still::credential_fixture
