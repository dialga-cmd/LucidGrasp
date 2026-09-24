#pragma once

#include "core/features.h"

#include <functional>
#include <vector>

#include <QString>

#include "core/clip_embedder.h"

namespace core {

struct IndexEntry {
  QString relPath;
  Features features;
  std::vector<float> clip;
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

  bool build(const QString &rootDir, ProgressFn progress = {});
  bool save(const QString &filePath) const;
  bool load(const QString &filePath);

  std::vector<SearchResult> search(const Features &query,
                                  const QImage &queryImage,
                                  int topK = 20) const;
  bool searchFile(const QString &queryPath, int topK,
                  std::vector<SearchResult> &out) const;

  const QString &root() const { return root_; }
  int errorCount() const { return errors_; }
  bool empty() const { return entries_.empty(); }
  size_t size() const { return entries_.size(); }
  void clear();

  void setClipEmbedder(ClipEmbedder *c) { clip_ = c; }

private:
  ClipEmbedder *clip_ = nullptr;

private:
  QString root_;
  std::vector<IndexEntry> entries_;
  int errors_ = 0;
};

QString defaultIndexPath(const QString &rootDir);
bool isSupportedImage(const QString &path);

} // namespace core
