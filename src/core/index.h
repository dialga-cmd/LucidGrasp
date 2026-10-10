#pragma once

#include "core/background_remover.h"
#include "core/cv_matcher.h"
#include "core/features.h"
#include "core/semantic_embedder.h"

#include <functional>
#include <vector>

#include <QString>

namespace core {

struct IndexEntry {
  QString relPath;
  Features features;
  // Semantic embedding of the image; empty when the index was built without
  // the embedder (visual-only) or the image could not be embedded.
  std::vector<float> embedding;
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

  // Builds the index. When `embedder` is loaded, a semantic embedding is
  // computed for every image as well, so a later semantic search can rank the
  // whole library without re-decoding anything.
  bool build(const QString &rootDir, ProgressFn progress = {},
             const SemanticEmbedder *embedder = nullptr);
  bool save(const QString &filePath) const;
  bool load(const QString &filePath);

  static constexpr size_t kMinShortlist = 256;
  static constexpr size_t kMaxShortlist = 2048;

  std::vector<SearchResult> search(const Features &query, const QImage &queryImage,
                                   double threshold,
                                   SearchProgressFn progress = {}) const;
  bool searchFile(const QString &queryPath, double threshold,
                  std::vector<SearchResult> &out,
                  SearchProgressFn progress = {}) const;

  // True when at least one entry carries a semantic embedding, i.e. the index
  // was built (or loaded) with the embedder. An index without embeddings must
  // be rebuilt before a semantic search can be run against it.
  bool hasSemanticEmbeddings() const;

  // Semantic (meaning-based) search. The query image is embedded with
  // `embedder` and every indexed image is ranked by the cosine similarity of
  // its stored embedding. Images indexed without an embedding are skipped.
  // Returns false when the embedder is not loaded or the query is unreadable.
  bool searchSemantic(const QString &queryPath,
                      const SemanticEmbedder &embedder, double threshold,
                      std::vector<SearchResult> &out,
                      SearchProgressFn progress = {}) const;

  // Stage-2 (rerank) candidates score at or above this cutoff; at most
  // kObjectRerankMax of them go through background removal.
  static constexpr double kObjectRerankCutoff = 0.5;
  static constexpr size_t kObjectRerankMax = 24;

  // Two-stage object search. Stage 1 runs the regular search on the query
  // matte; stage 2 strips the background of the strongest stage-1 matches
  // and re-scores them matte-to-matte, so a busy background in the library
  // image cannot inflate a false match. `remover` must have a model loaded
  // before this is called. Progress spans two phases: the search takes the
  // first 70%, the background removal + re-scoring the remaining 30%.
  bool searchWithObjectRerank(const QString &queryPath,
                              BackgroundRemover &remover, double threshold,
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
