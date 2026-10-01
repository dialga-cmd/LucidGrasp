#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <QImage>
#include <QString>

#include "core/embedding_cache.h"

namespace cv {
class Mat;
}

namespace core {

// Where the DINOv2-small int8 weights are expected. The weights are not
// bundled and are not downloaded by the app: this path is reported to the user
// when it is missing, and the file has to be placed there for Similar mode to
// work.
QString embedderModelPath();

// A real model file is ~23 MB. A wrong-but-200 response from a download URL is
// a ~52 KB error page, so the size is checked before the file is handed to the
// runtime rather than after it fails in some less obvious way.
bool embedderModelPresent();

// RAII wrapper over one ONNX Runtime session. Destructor releases the session
// and the env reference in the right order.
//
// OpenCV's own dnn module cannot load this model (its ONNX parser rejects the
// dynamic input shapes, and freezing them turns the parse error into a
// segfault), which is why the runtime here is ONNX Runtime rather than the
// ${OpenCV_LIBS} the rest of the app links.
class Embedder {
public:
    Embedder();
    ~Embedder();
    Embedder(const Embedder &) = delete;
    Embedder &operator=(const Embedder &) = delete;

    // Loads the model. False with lastError() set when the file is absent, is
    // not a model, or the session cannot be created.
    bool load(const QString &modelPath = QString());
    bool ready() const;
    const QString &lastError() const {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return error_;
    }

    // Embeds a file, decoded as colour BGR with EXIF orientation applied.
    bool embedFile(const QString &path, std::vector<float> &out);
    // Embeds an already-decoded colour image.
    bool embedImage(const QImage &image, std::vector<float> &out);

    // CLS token of the last_hidden_state output, L2-normalised.
    static int dim() { return kEmbeddingDim; }

    // Quantises a float embedding into a cache row.
    static EmbeddingRow toRow(uint64_t fileHash, const std::vector<float> &vec);

private:
    // The shared tail of embedFile()/embedImage(): resize, crop, colour,
    // normalise, run, take the CLS token, L2-normalise.
    bool embedBGR(const cv::Mat &bgr, std::vector<float> &out);

    // error_ is written from every builder worker on every failure, and
    // QString is not atomic, so the concurrent assignments go through here.
    void setError(const QString &message);

    struct Impl;
    std::unique_ptr<Impl> d_;
    mutable std::mutex errorMutex_;
    QString error_;
};

// Process-wide session, created on first use. One session is shared by the
// background builder's threads: Ort::Session::Run is thread-safe, and a session
// costs about 23 MB of resident weights, so one per worker thread would be
// pure waste.
Embedder &sharedEmbedder();

} // namespace core
