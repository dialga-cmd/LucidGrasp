#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <QByteArray>
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

// SHA-256 of the model file, or an empty array when it is absent or unreadable.
//
// Storing this in the cache header is what stops two different models from
// being mixed. The model id alone cannot do that job: "dinov2-small-int8"
// describes a family of exports, and the quantised graphs available for this
// model are not interchangeable (one of them produces different embeddings
// because its conv patch-embedding is folded differently). Embeddings from two
// models rank against each other with no error and no warning -- the scores
// just quietly mean nothing. The hash pins the exact bytes, so a cache built
// with one file is rejected when another is put in its place.
//
// Computed once per process and memoised: 24 MB of hashing on every query
// would be absurd, and the file does not change under a running app.
QByteArray embedderModelHash();

// How much of the frame reaches the model.
//
// Center is the measured configuration and stays the default. The others exist
// because a centre crop can miss the subject: in a wide garden scene the tulip
// is often small and off to one side, and a 224-pixel centre window sees mostly
// grass. Whether that is worth 6x the inference cost is an empirical question,
// so all three are reachable and selectable from the environment.
enum class CropMode {
  // Shortest edge to 256, centre 224 window. What every retrieval number in
  // SESSION.md was measured with.
  Center,
  // The whole frame resized to 224x224, aspect ratio squashed away. Keeps every
  // pixel of the subject at the cost of distorting it, and works on images too
  // small to crop.
  Full,
  // Centre plus the four corners plus the squash, each embedded separately, the
  // five-plus-one L2-normalised vectors averaged and re-normalised. Nothing is
  // cropped away, at six inference runs per image.
  Multi
};

// Short name as it appears in LUCIDGRASP_CROP and in the cache header.
QString cropModeName(CropMode mode);
CropMode cropModeFromName(const QString &name);

// The library-side mode, from LUCIDGRASP_CROP (center | full | multi).
// Unrecognised or unset values fall back to Center, so a typo degrades to the
// measured configuration instead of to a different one.
CropMode embedderCropMode();

// The query-side mode. LUCIDGRASP_QUERY_CROP overrides LUCIDGRASP_CROP for the
// query only, which is how "six views of the query against a centre-cropped
// library" can be measured without rebuilding the cache. The library cache
// records the mode it was built with and refuses a mismatched one, so a library
// mode and a query mode can differ without ever being confused with a cache.
CropMode embedderQueryCropMode();

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
    // The mode is explicit rather than defaulted: the library and the query can
    // be embedded differently on purpose, and a default argument would hide
    // which of the two a caller got.
    bool embedFile(const QString &path, std::vector<float> &out,
                   CropMode mode = CropMode::Center);
    // Embeds an already-decoded colour image.
    bool embedImage(const QImage &image, std::vector<float> &out,
                    CropMode mode = CropMode::Center);

    // CLS token of the last_hidden_state output, L2-normalised.
    static int dim() { return kEmbeddingDim; }

    // Quantises a float embedding into a cache row.
    static EmbeddingRow toRow(uint64_t fileHash, const std::vector<float> &vec);

private:
    // The shared tail of embedFile()/embedImage(): pick the view(s) the mode
    // asks for, colour-convert, run, take the CLS token, L2-normalise. Multi
    // averages the per-view vectors before the final normalise.
    bool embedBGR(const cv::Mat &bgr, std::vector<float> &out, CropMode mode);
    // One inference on one 224x224 BGR view: tensor, session run, CLS token,
    // L2-normalised. Factored out so Multi can run it six times without
    // duplicating any of the tensor or session code.
    bool embedView(const cv::Mat &bgrView, std::vector<float> &out);

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
