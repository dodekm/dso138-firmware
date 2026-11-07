#pragma once

#include "capture_model.hpp"

#include <QString>
#include <optional>

namespace dso
{

bool saveCapture(const QString &path, const Capture &, QString *error = nullptr);
std::optional<Capture> loadCapture(const QString &path, QString *error = nullptr);
bool exportCsv(const QString &path, const Capture &, QString *error = nullptr);

} // namespace dso
