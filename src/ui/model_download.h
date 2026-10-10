#pragma once

#include <QString>

class QWidget;

namespace ui {

// Model variant used by the object-based similar search and by the semantic
// (meaning-based) search.
enum class SearchModel {
  Lite,
  General,
  Semantic,
};

// Absolute path where a model variant lives in the per-user data directory.
QString modelStorePath(SearchModel model);

// Short human name used for radio options and dialog text.
QString modelDisplayName(SearchModel model);

// Outcome of the offline availability/version check for a model variant.
enum class ModelStatus {
  Ready,           // file present, size consistent and version current
  Missing,         // not downloaded yet
  Corrupt,         // file present but the size does not match the expected one
  UpdateAvailable  // a newer model revision is pinned by this build
};

// Cheap local check based on file presence, size and the stored revision.
// Performs no network access and should be cheap enough to run on selection.
ModelStatus checkModelStatus(SearchModel model);

// Interactive switch support: runs the check for `model` and, when it is not
// ready, shows a dialog offering to cancel the switch or download/update the
// model (with progress and a visible log). Returns true when the search mode
// may be applied; false when cancelled or the download failed.
bool prepareModelForSearch(SearchModel model, QWidget *parent);

// Legacy helper for the standalone background-removal action. Returns the
// path of the general model once it is present, or an empty string when
// cancelled or on failure.
QString backgroundModelStorePath();
QString ensureBackgroundModel(QWidget *parent);

}  // namespace ui