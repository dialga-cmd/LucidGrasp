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
  using ProgressFn = std::function<bool(const BuildProgress &)>;

  using SearchProgressFn = std::function<bool(int, int)>;

  bool build(const QString &rootDir, ProgressFn progress = {});
  bool save(const QString &filePath) const;
  bool load(const QString &filePath);

  static constexpr size_t kMinShortlist = 256;

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

  bool removeEntry(const QString &relPath);

private:
  QString root_;
  std::vector<IndexEntry> entries_;
  int errors_ = 0;
};

QString defaultIndexPath(const QString &rootDir);

QString legacyIndexPath(const QString &rootDir);

bool isSupportedImage(const QString &path);

}
