#pragma once

// A background pass that turns a library's images into DINOv2-small embeddings
// and writes them into the embedding cache beside the index.
//
// Deliberately separate from ImageIndex: the index is a fast, always-built
// artifact, and this is a job on a 23 MB model that a user starts by hand.
// Nothing here runs unless a user asked for it.

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QString>

#include "core/embedding_cache.h"

namespace core {

// Images embedded and total known at the time of the report. `done` counts
// images actually written, so it stops short of `total` when a file will not
// decode.
using EmbedProgressFn = std::function<void(int /*done*/, int /*total*/,
                                           const QString &current)>;

class EmbeddingBuilder {
public:
    EmbeddingBuilder() = default;
    ~EmbeddingBuilder();

    // Starts the build on a small pool of worker threads. Returns false, with
    // errorString() set, when the model is missing, a build is already running,
    // or the cache file cannot be opened. Never called automatically.
    bool start(const QString &cachePath, const std::vector<QString> &absPaths,
               const std::vector<uint64_t> &fileHashes,
               EmbedProgressFn progress = {});

    bool running() const { return running_.load(); }
    bool cancelRequested() const { return cancel_.load(); }
    void cancel() { cancel_ = true; }

    // Joins the workers. Returns the cache the run produced, so the caller can
    // install it into the index. Returns an empty cache after a cancel, which
    // is still the valid cache for whatever was checkpointed.
    EmbeddingCache take();
    // join() without taking the cache, for a shutdown that wants neither.
    void join();

    const QString &errorString() const { return error_; }
    int embedded() const { return embedded_.load(); }
    int failed() const { return failed_.load(); }

    // Half the cores, never fewer than one. Inference already uses every core
    // inside ONNX Runtime, so spawning a thread per core oversubscribes the
    // machine and makes the desktop unusable for the length of the job.
    static int threadCount();

private:
    EmbeddingBuilder(const EmbeddingBuilder &) = delete;
    EmbeddingBuilder &operator=(const EmbeddingBuilder &) = delete;

    // The workers' view of the job, owned here rather than by start()'s frame.
    // start() returns as soon as the pool is launched and the pool is joined
    // later, in take() or the destructor, so anything a worker touches has to
    // outlive the call that created it. Guarded by mutex_ except the atomics.
    std::vector<QString> todo_;
    std::vector<uint64_t> todoHashes_;
    EmbedProgressFn progress_;
    std::atomic<size_t> next_{0};
    std::atomic<int> done_{0};
    std::atomic<bool> writeFailed_{false};
    QString writeError_;

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    EmbeddingCache cache_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<int> embedded_{0};
    std::atomic<int> failed_{0};
    QString error_;
};

} // namespace core
