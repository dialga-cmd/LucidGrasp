#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>

namespace core {

// CLS token of DINOv2-small: 384 float32 after L2 normalisation.
inline constexpr int kEmbeddingDim = 384;

// Human-readable identifier stored in the cache header, for a user reading a
// hex dump or a bug report. It is deliberately NOT what decides whether a
// cache may be used -- see EmbeddingCache::load, which compares the model file's
// SHA-256 instead, because this string names a family of exports rather than
// one file.
inline const char *kEmbeddingModelId = "dinov2-small-int8";

// One stored embedding. int8 because the cache has to be re-read in full every
// time the library changes, and at 392 bytes a row that is ~143 MB for a
// 364k-image library against 533 MB as float32. The quantisation is lossy, but
// the measured fp32/int8 agreement for this model is 0.990 cosine, so ranking
// is unaffected; the query itself is kept float32 and never quantised.
//
// Each row is quantised by its own largest component, so the row's length is
// not 1 and carries no stored scale. That is not a problem: the direction is
// what is stored, and ImageIndex::search divides by the row's own length, which
// recovers the cosine without a scale field.
struct EmbeddingRow {
    uint64_t fileHash = 0;
    std::array<int8_t, kEmbeddingDim> vec{};
};

// The embedding cache: a flat file of <fileHash, int8[384]> rows, keyed by
// Features::fileHash so it joins to the existing index with no rebuild and no
// index format change.
//
// The header pins the cache to the exact inputs that produced it: the model
// file's SHA-256 and the preprocessing mode. Both are checked on load, because
// vectors from different inputs are not merely differently accurate -- they are
// not comparable at all, and nothing in the numbers would show it. A cosine
// between two unrelated embedding spaces is a small positive float, which sorts
// confidently and means nothing. So a mismatch is a refusal, not a warning, and
// the answer is always the same: rebuild.
//
// The writer appends and flushes in batches so a long build is resumable, and
// rewrites the header's count only once a batch is on disk. A kill at any point
// therefore leaves a file that loads with a shorter count, rather than a count
// pointing at rows that were never written.
class EmbeddingCache {
public:
    // What the cache on disk was produced from. The caller supplies both rather
    // than this class computing them: EmbeddingCache is deliberately ignorant
    // of where the model file is and of what preprocessing means, so it stays a
    // plain format reader/writer that can be tested without a model.
    struct SourceId {
        // SHA-256 of the model file, 32 raw bytes. Empty means the model is
        // unreadable, and an empty expected hash is treated as "do not check":
        // with no model there is no Similar search to run (ImageIndex::search
        // bails out on embedderModelPresent() first), and refusing here would
        // only mean a build without the weights could not tell a cache from a
        // foreign one -- which it cannot use anyway.
        QByteArray modelHash;
        // Preprocessing mode name, as passed by the embedder.
        QString cropMode;
    };

    EmbeddingCache() = default;
    // Non-copyable because it owns an open QFile. Movable, which is all
    // ImageIndex needs: the UI thread adopts a finished build by move.
    EmbeddingCache(const EmbeddingCache &) = delete;
    EmbeddingCache &operator=(const EmbeddingCache &) = delete;
    EmbeddingCache(EmbeddingCache &&) noexcept = default;
    EmbeddingCache &operator=(EmbeddingCache &&) noexcept = default;

    // Reads the whole cache. A missing or corrupt file is not an error worth
    // reporting upward: it means "no cache yet", and every caller has a
    // sensible answer for that. The count is validated against the file size
    // first, as ImageIndex::load does, so a truncated or hand-edited file is
    // refused instead of driving an enormous allocation.
    //
    // Refused, with lastError() explaining which, when the file's source does
    // not match `expected`: a different model file, or a different
    // preprocessing mode.
    bool load(const QString &path, const SourceId &expected);

    // Opens the file for writing. reset == true truncates it and writes a fresh
    // header; reset == false appends to an already-loaded cache, dropping any
    // partial trailing row left behind by an earlier run. The SourceId goes
    // into the header, and is not checked against a loaded cache here -- the
    // caller has already established, by loading with the same SourceId, that
    // the two agree.
    bool beginWrite(const QString &path, bool reset, const SourceId &source);

    // Appends rows, flushes them, then updates the header count. One
    // checkpoint per call, which is what makes a cancelled or killed build
    // resumable rather than all-or-nothing.
    bool append(const std::vector<EmbeddingRow> &rows);

    void endWrite();

    int count() const { return count_; }
    int dim() const { return dim_; }
    bool isValid() const { return count_ > 0; }
    // Row index for a file hash, or -1. This is the join into the index.
    int rowFor(uint64_t fileHash) const;
    uint64_t fileHashAt(int row) const;
    const int8_t *vectorAt(int row) const;

    void clear();
    const QString &lastError() const { return error_; }

    // What this cache was built from, as recorded in its header. Empty until
    // something is loaded or written.
    const SourceId &source() const { return source_; }

private:
    bool writeHeader();
    static constexpr quint32 kMagic = 0x4D45474C; // "LGEM"
    // 2 added the model SHA-256 and the crop mode. A v1 header has no room for
    // either, and reading one as v2 would silently compare garbage, so the
    // version check refuses it and the answer is a rebuild -- which is what a
    // v1 cache needed anyway, since it cannot be proven to match this model.
    static constexpr quint32 kVersion = 2;

    std::unique_ptr<QFile> file_;
    int count_ = 0;
    int dim_ = kEmbeddingDim;
    int headerBytes_ = 0;
    int countOffset_ = 0;
    QByteArray blob_; // header + rows, held for reads
    QHash<uint64_t, int> rows_;
    SourceId source_;
    QString error_;
};

} // namespace core
