#pragma once

#include "core/embedding_cache.h"
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
  // then only the shortlist is decoded and re-scored with OpenCV. The cap is
  // what keeps per-query cost proportional to the shortlist rather than to the
  // size of the library, which is what makes whole-filesystem indexes usable.
  // Byte-identical copies are always kept, whatever they score on the prefilter.
  static constexpr size_t kMinShortlist = 256;

  std::vector<SearchResult> search(const Features &query, const QImage &queryImage,
                                   double threshold,
                                   SearchProgressFn progress = {}) const;
  bool searchFile(const QString &queryPath, double threshold,
                  std::vector<SearchResult> &out,
                  SearchProgressFn progress = {}) const;

  // Which of the two ways a query is matched against the library. Lookalike is
  // the whole-frame comparison above; Similar matches on what the picture shows
  // rather than on how the frame looks, which is the only way a red tulip in one
  // garden ranks with a green one in another.
  enum class SearchMode { Lookalike, Similar };

  // Similar mode: brute-force dot product of the query embedding against every
  // cached embedding, top `kSimilarResults` by rank, no threshold. Embeddings
  // and cosine similarities are not percentages, so the Lookalike threshold does
  // not transfer to this mode and is not applied -- see the note in index.cpp.
  static constexpr size_t kSimilarResults = 50;
  // Whether Similar mode exists in this build at all (a compile-time fact), as
  // opposed to whether it is usable right now (embedderModelPresent()).
  static constexpr bool similarCompiled() {
#ifdef LUCIDGRASP_HAVE_ORT
    return true;
#else
    return false;
#endif
  }
  static bool similarAvailable();

  // Runs one of the two modes. `threshold` is the Lookalike percentage cutoff
  // and is honoured exactly as it was before Similar mode existed; Similar
  // ignores it, for the reason on kSimilarResults. Lookalike forwards to the
  // two-stage path above, unchanged; Similar embeds the query with the same
  // preprocessing the cache was built with and ranks the cache. Entries with no
  // embedding are skipped, so a partially built cache returns fewer results
  // rather than wrong ones.
  bool search(SearchMode mode, const QString &queryPath, double threshold,
              std::vector<SearchResult> &out, SearchProgressFn progress = {}) const;

  // Reloads the embedding cache that sits beside the index file. Called after
  // load() and after a build finishes, so coverage shown in the UI is never
  // stale. Also called from load() below for the same reason: a cached index
  // and a cached embedding set belong together, and requiring a second
  // explicit action to pick up the second one would make the mode look empty
  // on a library that is fully embedded.
  bool loadEmbeddings(const QString &path);
  void clearEmbeddings();
  // Installs a cache a background build produced. Moves rather than copies: the
  // cache is a whole-file blob and a copy of one would be a second full read of
  // a file that can be 143 MB.
  void adoptEmbeddings(EmbeddingCache &&cache) { embeddings_ = std::move(cache); }
  // Absolute paths and their file hashes, in index order, for the builder.
  std::vector<QString> absPaths() const;
  std::vector<uint64_t> fileHashes() const;
  // How many index entries currently have an embedding, for the "Similar
  // results cover N of M images" line.
  int embeddedCount() const;
  int embeddingCacheCount() const { return embeddings_.count(); }
  // Index entries with no embedding yet: what a build would still have to do.
  int missingEmbeddingCount() const;

  const QString &root() const { return root_; }
  int errorCount() const { return errors_; }
  bool empty() const { return entries_.empty(); }
  size_t size() const { return entries_.size(); }
  void clear();

private:
  QString root_;
  std::vector<IndexEntry> entries_;
  int errors_ = 0;
  EmbeddingCache embeddings_;
};

// Where the index for a given library root is cached. Deliberately outside the
// scanned tree so that indexing a read-only root such as "/" still works and
// the index is never itself indexed.
QString defaultIndexPath(const QString &rootDir);

// Where the embedding cache for that same library lives: the same directory and
// the same <label>-<digest> key, with a different extension. A separate file
// because it is built on a completely different schedule (only on explicit
// request, resumably) and a different scale (a 23 MB model against a 2.26 h
// job), so folding it into the index would mean invalidating a working index
// for an unrelated reason.
QString defaultEmbeddingPath(const QString &rootDir);

// Pre-1.1 location, inside the scanned root. Read-only so existing installs
// keep finding their cache.
QString legacyIndexPath(const QString &rootDir);

bool isSupportedImage(const QString &path);

} // namespace core
