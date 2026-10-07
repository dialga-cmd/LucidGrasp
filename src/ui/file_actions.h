#pragma once

#include <functional>

#include <QString>

namespace ui {

enum class RevealOutcome {
  Selected,
  FolderOnly,
  Failed
};

using RevealDone = std::function<void(RevealOutcome)>;

void revealInFileManager(const QString &path, const RevealDone &done);

bool moveToTrash(const QString &path, QString *error = nullptr);

}