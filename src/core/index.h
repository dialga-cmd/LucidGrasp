#pragma once

#include "core/features.h"

#include <functional>
#include <vector>

#include <QString>

#include "core/cv_matcher.h"

namespace core {

struct IndexEntry {
  QString relPath;
  Features features;
};

struct SearchResult {
  QString relPath;
  QString absPath;
  double score = 0.0;
  bool exact = false;
};

struct BuildProgress {
  int done = 0;
  int total = 0;
  QString current;
};

class ImageIndex {
public:
  // Return false from the progress callback to cancel the build.
  using ProgressFn = std::function<bool(const BuildProgress &)>;

  // Return false from the progress callback to cancel the search.
  using SearchProgressFn = std::function<bool(int /*done*/, int /*total*/)>;

  bool build(const QString &rootDir, ProgressFn progress = {});
  bool save(const QString &filePath) const;
  bool load(const QString &filePath);

  // Two-stage search. Every entry is ranked in memory from its stored hashes,
  // then only the shortlist is decoded and re-scored with OpenCV. This keeps
  // per-query cost proportional to the shortlist rather than to the size of
  // the library, which is what makes whole-filesystem indexes usable.
  std::vector<SearchResult> search(const Features &query, const QImage &queryImage,
                                   double threshold,
                                   SearchProgressFn progress = {}) const;
  bool searchFile(const QString &queryPath, double threshold,
                  std::vector<SearchResult> &out,
                  SearchProgressFn progress = {}) const;

  const QString &root() const { return root_; }
  int errorCount() const { return errors_; }
  bool empty() const { return entries_.empty(); }
  size_t size() const { return entries_.size(); }
  void clear();

  // Directory names skipped during a recursive scan. Mounting a whole
  // filesystem otherwise walks kernel and device pseudo-filesystems, which are
  // unreadable, enormous, and contain no photographs.
  static bool isSkippedSystemDir(const QString &dirName);

private:
  QString root_;
  std::vector<IndexEntry> entries_;
  int errors_ = 0;
};

// Where the index for a given library root is cached. Deliberately outside the
// scanned tree so that indexing a read-only root such as "/" still works and
// the index is never itself indexed.
QString defaultIndexPath(const QString &rootDir);

// Pre-1.1 location, inside the scanned root. Read-only so existing installs
// keep finding their cache.
QString legacyIndexPath(const QString &rootDir);

bool isSupportedImage(const QString &path);

} // namespace core
