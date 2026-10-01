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

// Identifier stored in the cache header. A cache written by a different model
// is rejected on load rather than silently ranked against, which would produce
// results that look plausible and mean nothing.
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
// The writer appends and flushes in batches so a long build is resumable, and
// rewrites the header's count only once a batch is on disk. A kill at any point
// therefore leaves a file that loads with a shorter count, rather than a count
// pointing at rows that were never written.
class EmbeddingCache {
public:
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
    bool load(const QString &path);

    // Opens the file for writing. reset == true truncates it and writes a fresh
    // header; reset == false appends to an already-loaded cache, dropping any
    // partial trailing row left behind by an earlier run.
    bool beginWrite(const QString &path, bool reset);

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

private:
    bool writeHeader();
    static constexpr quint32 kMagic = 0x4D45474C; // "LGEM"
    static constexpr quint32 kVersion = 1;

    std::unique_ptr<QFile> file_;
    int count_ = 0;
    int dim_ = kEmbeddingDim;
    int headerBytes_ = 0;
    int countOffset_ = 0;
    QByteArray blob_; // header + rows, held for reads
    QHash<uint64_t, int> rows_;
    QString error_;
};

} // namespace core
