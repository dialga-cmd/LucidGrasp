#include "core/index.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

#include <QDataStream>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStack>
#include <QElapsedTimer>

namespace core {

namespace {

constexpr quint32 kMagic = 0x494D5349; // "IMSI"
// v2 adds the source file's size and mtime, so a search can tell that an
// indexed file has changed and refresh its features instead of comparing a
// query against hashes for pixels that are no longer there. v1 caches are
// rejected and rebuilt rather than silently trusted.
constexpr quint32 kVersion = 2;

// Guards against pathological nesting in a whole-filesystem scan.
constexpr int kMaxScanDepth = 64;

// Directories visited between discovery progress reports.
constexpr int kDiscoveryReportInterval = 256;

const QSet<QString> &systemDirNames()
{
  // Names are lower-case on every platform; the resolve-path check in
  // isSystemPath() is the authority, the prefilter is just a cheap "maybe".
  static const QSet<QString> names = {
#ifdef Q_OS_WIN
      // Windows mounts no kernel pseudo-filesystem under a drive letter, but a
      // whole-drive scan of C:\ would walk hours of OS internals and junctions
      // (DriverStore, System32, ...) for essentially no indexable images.
      // These names are only skipped when the resolved path puts them directly
      // under a drive root, so an unrelated user folder further down the tree
      // that happens to share a name is never touched.
      QStringLiteral("windows"),
      QStringLiteral("program files"),
      QStringLiteral("program files (x86)"),
      QStringLiteral("programdata"),
      QStringLiteral("system volume information"),
      QStringLiteral("$recycle.bin"),
      QStringLiteral("recovery"),
      QStringLiteral("perflogs"),
#else
      // Kernel and device pseudo-filesystems: virtual, non-persistent, and
      // either enormous or permission-hostile to walk.
      QStringLiteral("proc"), QStringLiteral("sys"),
      QStringLiteral("dev"),  QStringLiteral("run"),
#endif
  };
  return names;
}

bool isSystemPath(const QString &path)
{
  if (path.isEmpty())
    return false;
#ifdef Q_OS_WIN
  // A drive-root system folder, e.g. "C:/Windows". The resolved path uses
  // forward slashes; the drive letter may be either case.
  const QString cleaned = QDir::fromNativeSeparators(path);
  const int slash = cleaned.lastIndexOf(QLatin1Char('/'));
  if (slash < 2)
    return false;
  const QString parent = cleaned.left(slash);
  if (parent.size() != 2 || parent.at(1) != QLatin1Char(':'))
    return false; // parent is not a drive root
  const QString name = cleaned.mid(slash + 1);
  for (const QString &n : systemDirNames())
    if (name.compare(n, Qt::CaseInsensitive) == 0)
      return true;
  return false;
#else
  for (const QString &name : systemDirNames()) {
    const QString prefix = QLatin1Char('/') + name;
    if (path == prefix || path.startsWith(prefix + QLatin1Char('/')))
      return true;
  }
  return false;
#endif
}

// Absolute, symlink-resolved form of a library root. Falls back to the plain
// absolute path when the root does not exist yet.
QString resolveRoot(const QString &rootDir)
{
  const QString canonical = QFileInfo(rootDir).canonicalFilePath();
  return canonical.isEmpty() ? QDir(rootDir).absolutePath() : canonical;
}

// Walk the tree collecting candidate files. Returns false from `keepGoing` to
// abandon the walk. Unreadable directories are skipped rather than treated as
// errors.
//
// Symlinked directories are followed, because icon themes and shared asset
// directories are routinely symlinked and skipping them would silently drop
// large parts of a library. Cycles are prevented by de-duplicating resolved
// paths of symlinked directories, which is the only way a traversal can loop.
bool collectImages(const QString &startDir, QStringList &out,
                   const std::function<bool(int)> &keepGoing)
{
  QSet<QString> visitedSymlinks;
  QStack<QPair<QString, int>> pending;
  pending.push({startDir, 0});
  int seen = 0;

  while (!pending.isEmpty()) {
    if (keepGoing && !keepGoing(seen))
      return false;
    ++seen;

    const auto current = pending.pop();
    const QString &dir = current.first;
    const int depth = current.second;

    const QFileInfoList entries =
        QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                    QDir::Hidden | QDir::System,
                                QDir::Name);
    for (const QFileInfo &fi : entries) {
      if (fi.isDir()) {
        // The leaf name is only a cheap pre-filter; the resolved path is what
        // decides. Matching on the name alone silently swallowed any user
        // folder that happened to be called "dev", "sys", or "run", and those
        // losses were invisible because they never reached the error counter.
        // Lower-cased so the pre-filter matches on a case-insensitive file
        // system; the authoritative isSystemPath() call below still requires
        // the real path to be a system root, so this cannot over-match.
        if (systemDirNames().contains(fi.fileName().toLower()) &&
            isSystemPath(fi.canonicalFilePath())) {
          continue; // kernel or device pseudo-filesystem
        }
        const QString path = fi.absoluteFilePath();
        if (fi.isSymLink()) {
          const QString canonical = fi.canonicalFilePath();
          if (canonical.isEmpty())
            continue; // broken link
          if (isSystemPath(canonical))
            continue; // link into a kernel or device pseudo-filesystem
          if (visitedSymlinks.contains(canonical))
            continue; // already walked this target; breaks cycles
          visitedSymlinks.insert(canonical);
        }
        if (depth + 1 > kMaxScanDepth)
          continue;
        // Traverse via the link path so recorded relative paths stay under the
        // scanned root; the resolved path is used only for cycle detection.
        pending.push({path, depth + 1});
      } else if (fi.isFile() && isSupportedImage(fi.fileName())) {
        out.append(fi.absoluteFilePath());
      }
    }
  }
  return true;
}

} // namespace

QString defaultIndexPath(const QString &rootDir) {
  // Built from the generic data location rather than AppDataLocation, which
  // would repeat the organisation and application name in the path.
  QString base = QStandardPaths::writableLocation(
      QStandardPaths::GenericDataLocation);
  if (base.isEmpty())
    base = QDir::homePath() + QStringLiteral("/.local/share");
  base += QStringLiteral("/LucidGrasp");

  const QString dir = base + QStringLiteral("/indexes");
  QDir().mkpath(dir);

  // Resolve to a real path first: the cache is keyed by the library it
  // describes, so a relative root such as "." must not collide with a
  // different directory that happens to be spelled the same way.
  const QString root = resolveRoot(rootDir);

  // Readable label plus a digest of the resolved path, so two libraries with
  // the same folder name never collide.
  QString label = QDir(root).dirName();
  if (label.isEmpty())
    label = QStringLiteral("root");
  label.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                QStringLiteral("_"));
  if (label.size() > 40)
    label.truncate(40);

  const QString digest = QString::fromLatin1(
      QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha1)
          .toHex()
          .left(16));
  return QStringLiteral("%1/%2-%3.bin").arg(dir, label, digest);
}

QString legacyIndexPath(const QString &rootDir) {
  return QDir(resolveRoot(rootDir))
      .filePath(QStringLiteral(".image_search_index.bin"));
}

QStringList supportedImageExtensions() {
  static const QStringList cached = [] {
    QSet<QString> exts;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    for (const QByteArray &f : formats)
      exts.insert(QString::fromLatin1(f).toLower());
    // These always load through the Qt base image readers.
    exts << QStringLiteral("png") << QStringLiteral("jpg")
         << QStringLiteral("jpeg") << QStringLiteral("bmp")
         << QStringLiteral("gif");
    QStringList list(exts.cbegin(), exts.cend());
    list.sort();
    return list;
  }();
  return cached;
}

bool isSupportedImage(const QString &path) {
  const int dot = path.lastIndexOf(QLatin1Char('.'));
  if (dot <= 0 || dot == path.size() - 1)
    return false; // non-empty extension required
  return supportedImageExtensions().contains(path.mid(dot + 1).toLower());
}

bool ImageIndex::build(const QString &rootDir, ProgressFn progress) {
  clear();

  const QDir root(rootDir);
  if (!root.exists())
    return false;
  root_ = resolveRoot(rootDir);

  QElapsedTimer progressTimer;
  progressTimer.start();

  // Phase 1: discover candidate files. total == 0 signals "still scanning".
  // Reports are throttled using a timer so we don't spam the UI queue
  // and we don't freeze the UI.
  QStringList files;
  const bool complete = collectImages(
      root_, files, [&](int seen) {
        if (!progress)
          return true;
        if (progressTimer.elapsed() < 50)
          return true;
        progressTimer.restart();
        return progress({0, 0, QStringLiteral("%1 directories").arg(seen)});
      });
  if (!complete)
    return false; // cancelled
  if (progress)
    progress({0, 0, QStringLiteral("%1 images found").arg(files.size())});

  // Phase 2: extract features, with a real total for the progress bar.
  const int total = files.size();
  int done = 0;

  for (const QString &file : files) {
    if (progress) {
      if (progressTimer.elapsed() >= 50 || done == total - 1) {
        progressTimer.restart();
        if (!progress({done, total, file}))
          return false; // cancelled
      }
    }

    Features feat;
    if (extractFeatures(file, feat)) {
      IndexEntry e;
      e.relPath = QDir(root_).relativeFilePath(file);
      e.features = feat;
      entries_.push_back(std::move(e));
    } else {
      ++errors_;
    }
    ++done;
  }

  if (progress)
    progress({done, total, {}});
  return true;
}

bool ImageIndex::save(const QString &filePath) const {
  QFile f(filePath);
  if (!f.open(QIODevice::WriteOnly))
    return false;

  QDataStream out(&f);
  out.setVersion(QDataStream::Qt_6_2);
  out << kMagic << kVersion << root_ << qint32(errors_)
      << quint32(entries_.size());
  for (const IndexEntry &e : entries_) {
    out << e.relPath << quint64(e.features.fileHash)
        << quint64(e.features.phash) << quint64(e.features.dhash)
        << qint64(e.features.size) << qint64(e.features.mtimeMs);
    out.writeRawData(reinterpret_cast<const char *>(e.features.hist.data()),
                     kHistBins);
  }
  return out.status() == QDataStream::Ok;
}

bool ImageIndex::load(const QString &filePath) {
  clear();

  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly))
    return false;

  QDataStream in(&f);
  in.setVersion(QDataStream::Qt_6_2);

  quint32 magic = 0, version = 0, count = 0;
  qint32 errors = 0;
  in >> magic >> version >> root_ >> errors >> count;
  // Clear on every failure path: root_ has already been overwritten by the
  // stream above, and leaving it set against an empty entry list breaks the
  // class invariant that a root only describes a populated index.
  if (in.status() != QDataStream::Ok || magic != kMagic || version != kVersion) {
    clear();
    return false;
  }

  // Each entry needs at least a path, three hashes, two timestamps, and the
  // histogram, so a file claiming more entries than it could physically hold is
  // corrupt. Resizing to an unchecked count would attempt an enormous
  // allocation.
  constexpr qint64 kMinBytesPerEntry =
      2 * sizeof(quint64) + 3 * sizeof(quint64) + 2 * sizeof(qint64) + kHistBins;
  if (qint64(count) * kMinBytesPerEntry > f.size()) {
    clear();
    return false;
  }

  entries_.resize(count);
  errors_ = errors;
  for (IndexEntry &e : entries_) {
    quint64 fh = 0, ph = 0, dh = 0;
    qint64 size = -1, mtimeMs = 0;
    in >> e.relPath >> fh >> ph >> dh >> size >> mtimeMs;
    e.features.fileHash = fh;
    e.features.phash = ph;
    e.features.dhash = dh;
    e.features.size = size;
    e.features.mtimeMs = mtimeMs;
    if (in.readRawData(reinterpret_cast<char *>(e.features.hist.data()),
                       kHistBins) != kHistBins) {
      clear();
      return false;
    }
  }
  if (in.status() != QDataStream::Ok) {
    clear();
    return false;
  }
  return true;
}

std::vector<SearchResult> ImageIndex::search(const Features &query,
                                             const QImage &queryImage, double threshold,
                                             SearchProgressFn progress) const {
  std::vector<SearchResult> results;
  if (entries_.empty())
    return results;

  // --- Stage 1: in-memory shortlist -----------------------------------
  // Reads only the stored hashes. We use a relaxed prefilter threshold to catch
  // anything that OpenCV might push above the final threshold.
  struct Candidate {
    double prefilter = 0.0;
    uint32_t entry = 0;
    bool exact = false;
  };

  // Byte-identical copies are always retained, whatever the prefilter says, so
  // they are collected separately and never compete for a shortlist slot.
  std::vector<Candidate> exactHits;
  std::vector<Candidate> pool;
  // OpenCV contributes 0.8, prefilter 0.2. So max possible score is 0.8 + 0.2 * prefilter.
  // We only keep candidates where max possible score >= threshold.
  for (size_t i = 0; i < entries_.size(); ++i) {
    // Stage one is pure in-memory arithmetic, but it still walks the entire
    // index, so the cancellation callback is consulted here too (throttled:
    // once per 8192 entries plus the final one). Without this, Stop is inert
    // for the first pass over a six-figure whole-filesystem index.
    if (progress &&
        (i % 8192 == 0 || i + 1 == entries_.size()) &&
        !progress(static_cast<int>(i), static_cast<int>(entries_.size())))
      return {};
    const bool exact = isExactMatch(query, entries_[i].features);
    const double p = prefilterScore(query, entries_[i].features);
    if (exact) {
      exactHits.push_back({p, uint32_t(i), true});
    } else if (0.8 + 0.2 * p >= threshold) {
      pool.push_back({p, uint32_t(i), false});
    }
  }

  // Keep only the strongest kMinShortlist. This is the step that makes the
  // architecture work: without a cap the relaxed bound above is satisfied by
  // every entry for any threshold at or below 0.8, so the entire library would
  // be decoded and scored. nth_element picks the top slice without paying for a
  // full sort, which matters when the index holds a whole filesystem.
  const auto stronger = [](const Candidate &a, const Candidate &b) {
    return a.prefilter > b.prefilter;
  };
  if (pool.size() > kMinShortlist) {
    std::nth_element(pool.begin(), pool.begin() + kMinShortlist, pool.end(),
                     stronger);
    pool.resize(kMinShortlist);
  }
  std::sort(pool.begin(), pool.end(), stronger);

  std::vector<Candidate> ranked = std::move(exactHits);
  ranked.insert(ranked.end(),
                std::make_move_iterator(pool.begin()),
                std::make_move_iterator(pool.end()));

  // --- Stage 2: OpenCV re-score of the shortlist -------------------
  CvMatcher matcher;
  const CvMatcher::Prepared preparedQuery = matcher.prepare(queryImage);

  const QDir root(root_);
  const int total = int(ranked.size());
  int done = 0;

  QElapsedTimer progressTimer;
  progressTimer.start();

  for (size_t k = 0; k < ranked.size(); ++k) {
    if (progress) {
      if (progressTimer.elapsed() >= 50 || done == total - 1) {
        progressTimer.restart();
        if (!progress(done, total))
          return {}; // cancelled
      }
    }

    const IndexEntry &e = entries_[ranked[k].entry];
    SearchResult r;
    r.relPath = e.relPath;
    r.absPath = root.absoluteFilePath(e.relPath);
    r.exact = ranked[k].exact;

    // File gone since indexing: nothing left to score. Without this check a
    // deleted file kept its stale prefilter, decoded to null, and re-appeared
    // as a ghost at ~0.2 * prefilter whenever the threshold was low enough.
    if (!QFile::exists(r.absPath)) {
      ++done;
      continue;
    }

    // If the file changed since it was indexed, the stored hashes no longer
    // describe it, so the prefilter term would be scored against pixels that
    // are not there. Refresh it. This is the same work indexing does, and it
    // only happens for files that moved under a stale cache.
    double prefilter = ranked[k].prefilter;
    bool exact = ranked[k].exact;
    if (isStale(e.features, r.absPath)) {
      Features fresh;
      if (extractFeatures(r.absPath, fresh)) {
        prefilter = prefilterScore(query, fresh);
        exact = isExactMatch(query, fresh);
        r.exact = exact;
      }
    }

    // Decoded straight to a small size rather than at full resolution, which
    // matters because the shortlist is decoded once per query.
    const CvMatcher::Prepared preparedCandidate = matcher.prepare(
        loadScaled(r.absPath, 512));
    r.score = 0.8 * matcher.match(preparedQuery, preparedCandidate) +
              0.2 * prefilter;

    if (exact || r.score >= threshold) {
      results.push_back(std::move(r));
    }
    ++done;
  }
  if (progress)
    progress(done, total);

  std::sort(results.begin(), results.end(),
            [](const SearchResult &a, const SearchResult &b) {
              if (a.score != b.score)
                return a.score > b.score;
              return a.relPath < b.relPath;
            });
  return results;
}

bool ImageIndex::searchFile(const QString &queryPath, double threshold,
                            std::vector<SearchResult> &out,
                            SearchProgressFn progress) const {
  Features feat;
  if (!extractFeatures(queryPath, feat))
    return false;
  // Routed through loadScaled for the same reason as the candidates: it keeps
  // the query and the index on one resolution, which is what the histogram
  // comparison assumes, and it applies EXIF orientation to both sides.
  out = search(feat, loadScaled(queryPath, 512), threshold, std::move(progress));
  return true;
}

void ImageIndex::clear() {
  root_.clear();
  entries_.clear();
  errors_ = 0;
}

} // namespace core
