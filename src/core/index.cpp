#include "core/index.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

#include <QDataStream>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStack>
#include <QElapsedTimer>

namespace core {

namespace {

constexpr quint32 kMagic = 0x494D5349;
constexpr quint32 kVersion = 2;

constexpr int kMaxScanDepth = 64;

const QSet<QString> &systemDirNames()
{
  static const QSet<QString> names = {
#ifdef Q_OS_WIN
      QStringLiteral("windows"),
      QStringLiteral("program files"),
      QStringLiteral("program files (x86)"),
      QStringLiteral("programdata"),
      QStringLiteral("system volume information"),
      QStringLiteral("$recycle.bin"),
      QStringLiteral("recovery"),
      QStringLiteral("perflogs"),
#else
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
  const QString cleaned = QDir::fromNativeSeparators(path);
  if (cleaned.size() < 3 || cleaned.at(1) != QLatin1Char(':') ||
      cleaned.at(2) != QLatin1Char('/'))
    return false;
  const QString rest = cleaned.mid(3);
  for (const QString &n : systemDirNames()) {
    if (rest.compare(n, Qt::CaseInsensitive) == 0 ||
        rest.startsWith(n + QLatin1Char('/'), Qt::CaseInsensitive))
      return true;
  }
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

QString resolveRoot(const QString &rootDir)
{
  const QString canonical = QFileInfo(rootDir).canonicalFilePath();
  return canonical.isEmpty() ? QDir(rootDir).absolutePath() : canonical;
}

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
        const QString canonical = fi.canonicalFilePath();
        const bool isLinkOrJunction = !canonical.isEmpty() && canonical != fi.absoluteFilePath();
        if ((systemDirNames().contains(fi.fileName().toLower()) ||
             isLinkOrJunction) &&
            isSystemPath(canonical)) {
          continue;
        }
        const QString path = fi.absoluteFilePath();
        if (fi.isSymLink()) {
          if (canonical.isEmpty())
            continue;
          if (visitedSymlinks.contains(canonical))
            continue;
          visitedSymlinks.insert(canonical);
        }
        if (depth + 1 > kMaxScanDepth)
          continue;
        pending.push({path, depth + 1});
      } else if (fi.isFile() && isSupportedImage(fi.fileName())) {
        out.append(fi.absoluteFilePath());
      }
    }
  }
  return true;
}

}

QString defaultIndexPath(const QString &rootDir) {
  QString base = QStandardPaths::writableLocation(
      QStandardPaths::GenericDataLocation);
  if (base.isEmpty())
    base = QDir::homePath() + QStringLiteral("/.local/share");
  base += QStringLiteral("/LucidGrasp");

  const QString dir = base + QStringLiteral("/indexes");
  QDir().mkpath(dir);

  const QString root = resolveRoot(rootDir);

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

const QSet<QString>& supportedImageExtensionSet() {
  static const QSet<QString> cached = [] {
    QSet<QString> exts;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    for (const QByteArray &f : formats)
      exts.insert(QString::fromLatin1(f).toLower());
    exts << QStringLiteral("png") << QStringLiteral("jpg")
         << QStringLiteral("jpeg") << QStringLiteral("bmp")
         << QStringLiteral("gif");
    return exts;
  }();
  return cached;
}

QStringList supportedImageExtensions() {
  static const QStringList cached = [] {
    QStringList list(supportedImageExtensionSet().cbegin(),
                     supportedImageExtensionSet().cend());
    list.sort();
    return list;
  }();
  return cached;
}

bool isSupportedImage(const QString &path) {
  const int dot = path.lastIndexOf(QLatin1Char('.'));
  if (dot <= 0 || dot == path.size() - 1)
    return false;
  return supportedImageExtensionSet().contains(path.mid(dot + 1).toLower());
}

bool ImageIndex::build(const QString &rootDir, ProgressFn progress) {
  clear();

  const QDir root(rootDir);
  if (!root.exists())
    return false;
  root_ = resolveRoot(rootDir);

  QElapsedTimer progressTimer;
  progressTimer.start();

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
    return false;
  if (progress)
    progress({0, 0, QStringLiteral("%1 images found").arg(files.size())});

  const int total = files.size();
  int done = 0;

  for (const QString &file : files) {
    if (progress) {
      if (progressTimer.elapsed() >= 50 || done == total - 1) {
        progressTimer.restart();
        if (!progress({done, total, file}))
          return false;
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
  QSaveFile f(filePath);
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

  if (out.status() != QDataStream::Ok)
    return false;

  return f.commit();
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
  if (in.status() != QDataStream::Ok || magic != kMagic || version != kVersion) {
    clear();
    return false;
  }

  constexpr qint64 kMinBytesPerEntry = 5 * sizeof(quint64) + kHistBins;
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

  struct Candidate {
    double prefilter = 0.0;
    uint32_t entry = 0;
    bool exact = false;
  };

  std::vector<Candidate> exactHits;
  std::vector<Candidate> pool;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (progress && (i % 8192 == 0 || i + 1 == entries_.size())) {
      const int cur =
          i + 1 == entries_.size()
              ? 50
              : int(qint64(i) * 50 / entries_.size());
      if (!progress(cur, 100))
        return {};
    }
    const bool exact = isExactMatch(query, entries_[i].features);
    const double p = prefilterScore(query, entries_[i].features);
    if (exact) {
      exactHits.push_back({p, uint32_t(i), true});
    } else if (0.8 + 0.2 * p >= threshold) {
      pool.push_back({p, uint32_t(i), false});
    }
  }

  const auto stronger = [](const Candidate &a, const Candidate &b) {
    return a.prefilter > b.prefilter;
  };
  size_t shortlist = kMinShortlist;
  const size_t count = entries_.size();
  if (count > 0) {
    const size_t scaled = std::min(count / 20, kMaxShortlist);
    if (scaled > shortlist)
      shortlist = scaled;
  }
  if (pool.size() > shortlist) {
    std::nth_element(pool.begin(), pool.begin() + shortlist, pool.end(),
                     stronger);
    pool.resize(shortlist);
  }
  std::sort(pool.begin(), pool.end(), stronger);

  std::vector<Candidate> ranked = std::move(exactHits);
  ranked.insert(ranked.end(),
                std::make_move_iterator(pool.begin()),
                std::make_move_iterator(pool.end()));

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
        const int cur =
            50 + (total > 0 ? int(qint64(done) * 50 / total) : 0);
        if (!progress(cur, 100))
          return {};
      }
    }

    const IndexEntry &e = entries_[ranked[k].entry];
    SearchResult r;
    r.relPath = e.relPath;
    r.absPath = root.absoluteFilePath(e.relPath);
    r.exact = ranked[k].exact;

    if (!QFile::exists(r.absPath)) {
      ++done;
      continue;
    }

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
    progress(100, 100);

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
  QImage queryImg;
  if (!extractFeatures(queryPath, feat, &queryImg))
    return false;
  out = search(feat, queryImg, threshold, std::move(progress));
  return true;
}

bool ImageIndex::searchWithObjectRerank(const QString &queryPath,
                                        BackgroundRemover &remover,
                                        double threshold,
                                        std::vector<SearchResult> &out,
                                        SearchProgressFn progress) const {
  Features feat;
  QImage queryImage;
  if (!extractFeatures(queryPath, feat, &queryImage))
    return false;

  const auto stage1Progress = [progress](int cur, int total) -> bool {
    if (!progress)
      return true;
    const int pct = int(double(cur) * 70 / 100);
    return progress(pct, total);
  };

  out = search(feat, queryImage, threshold, std::move(stage1Progress));
  if (out.empty())
    return true;

  std::vector<SearchResult> candidates;
  candidates.reserve(out.size());
  for (auto &r : out) {
    if (r.exact || r.score >= kObjectRerankCutoff)
      candidates.push_back(std::move(r));
  }
  if (candidates.size() > kObjectRerankMax) {
    std::partial_sort(
        candidates.begin(), candidates.begin() + kObjectRerankMax,
        candidates.end(),
        [](const SearchResult &a, const SearchResult &b) {
          return a.score > b.score;
        });
    candidates.resize(kObjectRerankMax);
  }

  CvMatcher matcher;
  const CvMatcher::Prepared queryMatte = matcher.prepare(queryImage);

  const int total = int(candidates.size());
  int done = 0;

  QElapsedTimer progressTimer;
  progressTimer.start();

  std::vector<SearchResult> reranked;
  reranked.reserve(candidates.size());

  for (auto &c : candidates) {
    if (progress) {
      if (progressTimer.elapsed() >= 50 || done == total - 1) {
        progressTimer.restart();
        const int pct = 70 + (total > 0 ? int(30.0 * done / total) : 0);
        if (!progress(pct, 100))
          return {};
      }
    }

    const QImage candidate = loadImageForBackground(c.absPath);
    QImage cutout;
    QString error;
    if (!candidate.isNull() &&
        remover.removeBackground(candidate, &cutout, &error)) {
      const CvMatcher::Prepared candidateMatte =
          matcher.prepare(matteOf(cutout));
      const double refined =
          0.8 * matcher.match(queryMatte, candidateMatte) + 0.2 * c.score;
      if (c.exact || refined >= threshold) {
        c.score = refined;
        reranked.push_back(std::move(c));
      }
      ++done;
      continue;
    }

    if (c.exact || c.score >= threshold)
      reranked.push_back(std::move(c));
    ++done;
  }
  if (progress)
    progress(100, 100);

  std::sort(reranked.begin(), reranked.end(),
            [](const SearchResult &a, const SearchResult &b) {
              if (a.score != b.score)
                return a.score > b.score;
              return a.relPath < b.relPath;
            });
  out = std::move(reranked);
  return true;
}

void ImageIndex::clear() {
  root_.clear();
  entries_.clear();
  errors_ = 0;
}

bool ImageIndex::removeEntry(const QString &relPath) {
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const IndexEntry &e) {
                                 return e.relPath == relPath;
                               });
  if (it == entries_.end())
    return false;
  entries_.erase(it);
  return true;
}

}
