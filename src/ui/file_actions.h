#pragma once

#include <functional>

#include <QString>

namespace ui {

// Whether revealing a file actually produced a selection.
//
// The distinction matters because "open the folder" and "open the folder with
// this file selected" are different things, and collapsing them into one bool
// would make the second silently degrade into the first with nothing to say so.
enum class RevealOutcome {
  Selected,   // the folder opened with the file selected
  FolderOnly, // the folder opened, but no file manager could select it
  Failed      // nothing opened at all
};

// Opens the platform file manager on the file's containing folder, selecting the
// file where the platform supports it.
//
// Completes asynchronously: `done` runs on the calling thread's event loop once
// the outcome is known. It is asynchronous because the standard Linux route is a
// D-Bus round trip to another process, and a file manager that is busy or hung
// would otherwise freeze this window for the length of the D-Bus timeout.
//
// `done` is invoked exactly once, on the calling thread's event loop, with the
// real outcome -- including the platforms that can only open the folder. It is
// never invoked synchronously, so nothing may be assumed on return. Callers must
// treat a destroyed receiver as a reason to drop the callback.
using RevealDone = std::function<void(RevealOutcome)>;

void revealInFileManager(const QString &path, const RevealDone &done);

// Moves a file to the platform's trash, where it stays recoverable: the Windows
// Recycle Bin, the macOS Trash, or the freedesktop.org trash specification on
// Linux. Returns false and, when given, sets *error to something worth showing a
// user.
//
// Deliberately not a hard delete. A "delete" button that cannot be undone turns
// a misclick into permanent loss of a photograph, and the platforms all provide
// the recoverable version at effectively no extra cost.
//
// On failure the file is left exactly as it was -- there is no fallback to
// unlink(), because a partially applied delete is the worst of the outcomes.
bool moveToTrash(const QString &path, QString *error = nullptr);

}  // namespace ui