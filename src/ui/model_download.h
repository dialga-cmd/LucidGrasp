#pragma once

#include <QString>

class QWidget;

namespace ui {

QString backgroundModelStorePath();

// Downloads the background model into the per-user app data directory when
// missing or corrupt. Returns an empty string when cancelled or on failure.
QString ensureBackgroundModel(QWidget *parent);

}  // namespace ui