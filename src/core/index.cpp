#include "core/index.h"

#include <algorithm>

#include <QDataStream>
#include <QDir>
#include <QImage>
#include <QDirIterator>
#include <QFile>
#include <QImageReader>
#include <QSet>

namespace core {

namespace {

constexpr quint32 kMagic = 0x494D5349; // "IMSI"
constexpr quint32 kVersion = 1;

} // namespace

QString defaultIndexPath(const QString &rootDir) {
  return QDir(rootDir).filePath(QStringLiteral(".image_search_index.bin"));
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
  root_ = root.absolutePath();

  QStringList files;
  QDirIterator it(root_, QDir::Files | QDir::Readable | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString p = it.next();
    if (isSupportedImage(p))
      files.append(p);
  }

  const int total = files.size();
  int done = 0;
  int lastReportedPct = -1;

  for (const QString &file : files) {
    if (progress) {
      const int pct = total > 0 ? done * 100 / total : 100;
      if (pct != lastReportedPct || done == total - 1) {
        lastReportedPct = pct;
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
        << quint64(e.features.phash) << quint64(e.features.dhash);
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
  if (in.status() != QDataStream::Ok || magic != kMagic || version != kVersion)
    return false;

  entries_.resize(count);
  errors_ = errors;
  for (IndexEntry &e : entries_) {
    quint64 fh = 0, ph = 0, dh = 0;
    in >> e.relPath >> fh >> ph >> dh;
    e.features.fileHash = fh;
    e.features.phash = ph;
    e.features.dhash = dh;
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
                                             const QImage &queryImage,
                                             int topK) const {
  std::vector<SearchResult> results;
  if (entries_.empty() || topK <= 0)
    return results;

  results.reserve(entries_.size());
  const QDir root(root_);
  
  CvMatcher cvMatcher;

  for (const IndexEntry &e : entries_) {
    SearchResult r;
    r.relPath = e.relPath;
    r.absPath = root.absoluteFilePath(e.relPath);
    
    // Perform detailed OpenCV pixel matching
    QImage dbImg(r.absPath);
    double cvScore = cvMatcher.match(queryImage, dbImg);
    
    // Combine feature score and OpenCV pixel score
    double baseScore = combineScore(query, e.features);
    r.score = 0.8 * cvScore + 0.2 * baseScore;
    
    r.exact = isExactMatch(query, e.features);
    results.push_back(std::move(r));
  }

  if (int(results.size()) > topK) {
    std::partial_sort(results.begin(), results.begin() + topK, results.end(),
                      [](const SearchResult &a, const SearchResult &b) {
                        if (a.score != b.score)
                          return a.score > b.score;
                        return a.relPath < b.relPath;
                      });
    results.resize(topK);
  } else {
    std::sort(results.begin(), results.end(),
              [](const SearchResult &a, const SearchResult &b) {
                if (a.score != b.score)
                  return a.score > b.score;
                return a.relPath < b.relPath;
              });
  }
  return results;
}

bool ImageIndex::searchFile(const QString &queryPath, int topK,
                            std::vector<SearchResult> &out) const {
  Features feat;
  if (!extractFeatures(queryPath, feat))
    return false;
  out = search(feat, QImage(queryPath), topK);
  return true;
}

void ImageIndex::clear() {
  root_.clear();
  entries_.clear();
  errors_ = 0;
}

} // namespace core
