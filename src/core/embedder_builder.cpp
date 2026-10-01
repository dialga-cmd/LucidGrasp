#include "core/embedder_builder.h"

#ifdef LUCIDGRASP_HAVE_ORT

#include "core/embedder.h"

#include <atomic>
#include <cstring>
#include <vector>

#include <QElapsedTimer>
#include <QThread>

namespace core {

namespace {

// Append + flush this often. Every 512 is a compromise: a kill costs at most
// one checkpoint of re-work, and the flush itself is not free on a large file.
constexpr int kCheckpointEvery = 512;

} // namespace

int EmbeddingBuilder::threadCount() {
  // Half the cores, and never fewer than one. ONNX Runtime is already using
  // every core inside a single Run(), so a thread per core would oversubscribe
  // the machine and make the desktop unusable for the length of the job.
  return std::max(1, QThread::idealThreadCount() / 2);
}

void EmbeddingBuilder::join() {
  for (std::thread &t : workers_)
    if (t.joinable())
      t.join();
  workers_.clear();
  running_ = false;
}

bool EmbeddingBuilder::start(const QString &cachePath,
                             const std::vector<QString> &absPaths,
                             const std::vector<uint64_t> &fileHashes,
                             EmbedProgressFn progress) {
  if (running_.load()) {
    error_ = QStringLiteral("a build is already running");
    return false;
  }
  if (absPaths.empty() || absPaths.size() != fileHashes.size()) {
    error_ = QStringLiteral("nothing to embed");
    return false;
  }
  if (!embedderModelPresent()) {
    // The one hard precondition. Reported rather than guessed around, because a
    // 2-hour job that produces nothing is the worst outcome available here.
    error_ = QStringLiteral("model not found: %1").arg(embedderModelPath());
    return false;
  }

  error_.clear();
  cancel_ = false;
  embedded_ = 0;
  failed_ = 0;

  {
    // A partial cache from an earlier, cancelled run is loaded first, so the
    // already-embedded tail is skipped and the job resumes rather than starting
    // over. A missing or corrupt file is not an error, but it does decide the
    // write mode: there is nothing to append to, so the file is created fresh.
    // Appending without a loaded cache has no header to append against, which is
    // how the very first build on a machine used to refuse to start.
    std::lock_guard<std::mutex> lock(mutex_);
    const bool resume = QFile::exists(cachePath) && cache_.load(cachePath);
    if (!resume)
      cache_.clear();
    if (!cache_.beginWrite(cachePath, !resume)) {
      error_ = cache_.lastError();
      return false;
    }
  }

  // Load the session up front, on this thread. Constructing it takes about a
  // second and a half, and a failure here (a truncated file, a missing runtime)
  // should be reported before any worker starts rather than as N identical
  // per-image errors.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Embedder &embedder = sharedEmbedder();
    if (!embedder.ready()) {
      error_ = embedder.lastError();
      cache_.endWrite();
      return false;
    }
  }

  // Only the entries that have no embedding yet. The join key is
  // Features::fileHash, which is content-addressed, so two byte-identical files
  // share one row and one unit of work.
  std::vector<QString> todo;
  std::vector<uint64_t> todoHashes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    todo.reserve(absPaths.size());
    for (size_t i = 0; i < absPaths.size(); ++i) {
      if (cache_.rowFor(fileHashes[i]) < 0) {
        todo.push_back(absPaths[i]);
        todoHashes.push_back(fileHashes[i]);
      }
    }
  }
  if (todo.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.endWrite();
    return true; // already complete; nothing to do
  }

  const int total = int(todo.size());
  // Move into members so worker threads keep a valid, stable view of the work
  // that outlives the start() call.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    todo_ = std::move(todo);
    todoHashes_ = std::move(todoHashes);
  }
  progress_ = progress;
  next_ = 0;
  done_ = 0;
  writeFailed_ = false;
  writeError_.clear();

  // One thread per outstanding image at most, so a 3-image library does not
  // wake six threads to do three forwards passes.
  const int threads = std::max(1, std::min<int>(threadCount(), total));
  running_ = true;

  for (int t = 0; t < threads; ++t) {
    // total by value: it is a local, and the worker outlives this frame.
    workers_.emplace_back([this, total] {
      std::vector<EmbeddingRow> batch;
      batch.reserve(kCheckpointEvery);
      std::vector<float> vec;

      const auto flush = [&](bool force) {
        if (batch.empty())
          return true;
        if (!force && int(batch.size()) < kCheckpointEvery)
          return true;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cache_.append(batch)) {
          writeError_ = cache_.lastError();
          writeFailed_ = true;
          return false;
        }
        batch.clear();
        return true;
      };

      for (;;) {
        if (cancel_.load() || writeFailed_.load())
          break;
        const size_t i = next_.fetch_add(1);
        if (i >= todo_.size())
          break;

        if (!sharedEmbedder().embedFile(todo_[i], vec)) {
          failed_++;
        } else {
          batch.push_back(Embedder::toRow(todoHashes_[i], vec));
          embedded_++;
          if (int(batch.size()) >= kCheckpointEvery)
            if (!flush(true))
              break;
        }

        const int nowDone = ++done_;
        if (progress_)
          progress_(nowDone, total, todo_[i]);
      }

      // A cancelled or exhausted worker still writes what it holds, so a Stop
      // keeps the images already inferred rather than discarding the last
      // sub-checkpoint of work.
      std::lock_guard<std::mutex> lock(mutex_);
      if (!batch.empty() && !cache_.append(batch)) {
        writeError_ = cache_.lastError();
        writeFailed_ = true;
      }
    });
  }

  return true;
}

EmbeddingBuilder::~EmbeddingBuilder() {
  cancel();
  join();
  std::lock_guard<std::mutex> lock(mutex_);
  // The done list is dead weight for the rest of the builder's life and holds
  // every path the user indexed, so it is released at the end of a run rather
  // than lingering until the window closes.
  todo_.clear();
  todoHashes_.clear();
  todo_.shrink_to_fit();
  todoHashes_.shrink_to_fit();
}

EmbeddingCache EmbeddingBuilder::take() {
  join();
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.endWrite();
  // The cache as a whole, not just this run's contribution: a cancelled build
  // still has everything it checkpointed to show for it.
  return std::move(cache_);
}

} // namespace core

#else // !LUCIDGRASP_HAVE_ORT

// Built without ONNX Runtime: there is nothing to embed, so the class exists
// only to keep the UI's call sites compiling. start() always fails, and the
// Similar mode it feeds is never offered.
namespace core {

int EmbeddingBuilder::threadCount() { return 1; }

EmbeddingBuilder::~EmbeddingBuilder() = default;

bool EmbeddingBuilder::start(const QString &, const std::vector<QString> &,
                             const std::vector<uint64_t> &, EmbedProgressFn) {
  error_ = QStringLiteral("built without ONNX Runtime");
  return false;
}

EmbeddingCache EmbeddingBuilder::take() { return EmbeddingCache(); }

} // namespace core

#endif
