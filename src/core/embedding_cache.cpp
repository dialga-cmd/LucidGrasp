#include "core/embedding_cache.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace core {

namespace {

constexpr int kHeaderTagBytes = 4; // magic + version
// One row on disk: the file hash and the quantised vector.
constexpr int kRowBytes = int(sizeof(uint64_t)) + kEmbeddingDim;

// The header and the count that overwrites it in place after every checkpoint
// are the same file format, so they are written and read through one set of
// stream settings. The count used to go in as a raw memcpy of a host quint32,
// which disagreed with the header's QDataStream encoding on the very first
// checkpoint and made the file unreadable to its own loader.
void configureStream(QDataStream &s) {
  s.setVersion(QDataStream::Qt_6_2);
  s.setByteOrder(QDataStream::LittleEndian);
}

} // namespace

bool EmbeddingCache::load(const QString &path) {
    clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error_ = f.errorString();
        return false;
    }
    blob_ = f.readAll();
    if (blob_.isEmpty()) {
        error_ = QStringLiteral("empty file");
        return false;
    }

    QDataStream in(blob_);
    configureStream(in);

    quint32 magic = 0, version = 0, storedDim = 0, storedCount = 0;
    QString modelId;
    in >> magic >> version >> modelId >> storedDim >> storedCount;

    if (in.status() != QDataStream::Ok) {
        error_ = QStringLiteral("truncated header");
        clear();
        return false;
    }
    if (magic != kMagic || version != kVersion) {
        error_ = QStringLiteral("not an embedding cache");
        clear();
        return false;
    }
    if (modelId != QLatin1String(kEmbeddingModelId) ||
        storedDim != quint32(kEmbeddingDim)) {
        error_ = QStringLiteral("written by a different model");
        clear();
        return false;
    }

    const int headerBytes = int(in.device()->pos());
    // The same guard ImageIndex::load applies: a count larger than the file
    // could physically hold is corruption, and resizing to it unchecked would
    // attempt an enormous allocation. 64-bit because a 32-bit count times
    // 392 bytes overflows long before the check would.
    const qint64 claimed = qint64(headerBytes) + qint64(storedCount) * kRowBytes;
    if (claimed > qint64(blob_.size())) {
        error_ = QStringLiteral("truncated: header claims %1 rows, file holds "
                                "%2")
                     .arg(storedCount)
                     .arg((qint64(blob_.size()) - headerBytes) / kRowBytes);
        clear();
        return false;
    }

    count_ = int(storedCount);
    dim_ = kEmbeddingDim;
    headerBytes_ = headerBytes;
    countOffset_ = headerBytes - int(sizeof(quint32));

    rows_.reserve(count_);
    for (int i = 0; i < count_; ++i) {
        const char *p = blob_.constData() + headerBytes_ + i * kRowBytes;
        uint64_t hash = 0;
        memcpy(&hash, p, sizeof(hash));
        // First writer wins on a duplicated hash, so a resumed build that
        // re-embeds a changed file cannot leave two live rows for one key.
        if (!rows_.contains(hash))
            rows_.insert(hash, i);
    }
    return true;
}

bool EmbeddingCache::writeHeader() {
    QByteArray head;
    QDataStream out(&head, QIODevice::WriteOnly);
    configureStream(out);
    out << kMagic << kVersion << QString::fromLatin1(kEmbeddingModelId)
        << quint32(kEmbeddingDim) << quint32(0);
    headerBytes_ = int(head.size());
    countOffset_ = headerBytes_ - int(sizeof(quint32));
    if (file_->write(head) != head.size())
        return false;
    return file_->flush();
}

bool EmbeddingCache::beginWrite(const QString &path, bool reset) {
    endWrite();

    QDir().mkpath(QFileInfo(path).absolutePath());
    auto f = std::make_unique<QFile>(path);
    // WriteOnly on its own truncates an existing file, which would destroy the
    // partial cache this call is about to append to. Appending therefore needs
    // ReadWrite: still seekable (the header's count is rewritten in place after
    // every checkpoint) and never truncating.
    QIODevice::OpenMode mode =
        reset ? (QIODevice::WriteOnly | QIODevice::Truncate) : QIODevice::ReadWrite;
    if (!f->open(mode)) {
        error_ = f->errorString();
        return false;
    }
    // Published before writeHeader() rather than at the end: the header write
    // goes through file_, and holding it in the local until then left file_
    // null on the very first build.
    file_ = std::move(f);

    if (reset) {
        count_ = 0;
        rows_.clear();
        blob_.clear();
        if (!writeHeader()) {
            error_ = file_->errorString();
            endWrite();
            return false;
        }
    } else {
        // Appending to a cache this object has not read would duplicate rows
        // and desynchronise the header, so the count has to be known.
        if (headerBytes_ <= 0) {
            error_ = QStringLiteral("no cache loaded to append to");
            endWrite();
            return false;
        }
        const qint64 expected = qint64(headerBytes_) + qint64(count_) * kRowBytes;
        if (qint64(file_->size()) < expected) {
            // A previous run died before its header update, so the file is
            // shorter than the count says. Trust the file.
            error_ = QStringLiteral("cache is shorter than its header claims");
            endWrite();
            return false;
        }
        if (qint64(file_->size()) > expected && !file_->resize(expected)) {
            // Trailing bytes are a half-written row from an earlier run.
            error_ = file_->errorString();
            endWrite();
            return false;
        }
        if (!file_->seek(expected)) {
            error_ = file_->errorString();
            endWrite();
            return false;
        }
    }

    return true;
}

bool EmbeddingCache::append(const std::vector<EmbeddingRow> &rows) {
    if (!file_ || rows.empty())
        return true;

    QByteArray block;
    block.reserve(int(rows.size()) * kRowBytes);
    for (const EmbeddingRow &r : rows) {
        block.append(reinterpret_cast<const char *>(&r.fileHash),
                     int(sizeof(uint64_t)));
        block.append(reinterpret_cast<const char *>(r.vec.data()), kEmbeddingDim);
    }

    // Derived from count_, never from the current file position. The position
    // is not usable here: the count update below rewrites 4 bytes in the
    // header, and after a resumed build that lands in the middle of the file
    // rather than back at its end.
    const qint64 rowPos = qint64(headerBytes_) + qint64(count_) * kRowBytes;
    if (!file_->seek(rowPos) || file_->write(block) != block.size()
        || !file_->flush()) {
        error_ = file_->errorString();
        return false;
    }

    // Count last, and only once the rows are durable. A kill between the two
    // leaves rows on disk that the header does not claim, which loads as a
    // shorter cache -- the safe direction, since those rows are re-embeddable.
    count_ += int(rows.size());
    // Written through the same stream settings as the header, so the field the
    // loader reads back is the field that was written.
    if (!file_->seek(countOffset_)) {
        error_ = file_->errorString();
        return false;
    }
    QDataStream out(file_.get());
    configureStream(out);
    out << quint32(count_);
    if (out.status() != QDataStream::Ok || !file_->flush()) {
        error_ = file_->errorString();
        return false;
    }

    blob_.append(block);
    for (int i = 0; i < int(rows.size()); ++i) {
        const int row = count_ - rows.size() + i;
        if (!rows_.contains(rows[i].fileHash))
            rows_.insert(rows[i].fileHash, row);
    }
    return true;
}

void EmbeddingCache::endWrite() {
    if (file_) {
        file_->flush();
        file_->close();
        file_.reset();
    }
}

int EmbeddingCache::rowFor(uint64_t fileHash) const {
    return rows_.value(fileHash, -1);
}

uint64_t EmbeddingCache::fileHashAt(int row) const {
    if (row < 0 || row >= count_)
        return 0;
    uint64_t hash = 0;
    memcpy(&hash, blob_.constData() + headerBytes_ + row * kRowBytes,
           sizeof(hash));
    return hash;
}

const int8_t *EmbeddingCache::vectorAt(int row) const {
    if (row < 0 || row >= count_)
        return nullptr;
    return reinterpret_cast<const int8_t *>(blob_.constData() +
                                            headerBytes_ + row * kRowBytes +
                                            int(sizeof(uint64_t)));
}

void EmbeddingCache::clear() {
    endWrite();
    count_ = 0;
    dim_ = kEmbeddingDim;
    headerBytes_ = 0;
    countOffset_ = 0;
    blob_.clear();
    rows_.clear();
}

} // namespace core
