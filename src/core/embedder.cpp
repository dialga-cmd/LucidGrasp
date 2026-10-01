#include "core/embedder.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef LUCIDGRASP_HAVE_ORT
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace core {

// The model path and its size gate are pure Qt, so they exist in every build:
// the UI reports this path when the weights are missing, and that has to be a
// real path in a build with no runtime to show it to.
QString embedderModelPath() {
  // Same base as the index cache (index.cpp's cachePathFor), so the model sits
  // beside the caches it serves. mkpath because the path is shown to the user as
  // the place to put the file.
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      QStringLiteral("/models");
  QDir().mkpath(dir);
  return dir + QStringLiteral("/dinov2_small_int8.onnx");
}

bool embedderModelPresent() {
  const QFileInfo info(embedderModelPath());
  // ~23 MB in reality. A mistyped download URL answers HTTP 200 with a ~52 KB
  // error page, so a size floor is what distinguishes "model" from "HTML that
  // was saved under the model's name".
  return info.exists() && info.isFile() && info.size() > (1 << 20);
}

#ifdef LUCIDGRASP_HAVE_ORT

namespace {

// The measured preprocessing, in this order and no other: shortest edge to 256
// with INTER_CUBIC, centre crop 224, BGR->RGB, /255, ImageNet mean and std,
// NCHW float32. Anything else here changes what the model sees, and the
// retrieval numbers in SESSION.md were measured with exactly this.
constexpr int kShortestEdge = 256;
constexpr int kCrop = 224;

// One ONNX Runtime environment per process, created before the first session
// and outliving it. A function-local static, so it is initialised exactly once
// even when the first call comes from a worker thread.
Ort::Env &ortEnv() {
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "lucidgrasp");
  return env;
}

// Writes the normalised tensor as NCHW. The mean/subtract/divide is done by
// hand rather than with a cv::Mat expression, because the expression promotes
// through a CV_64F Scalar and the exact float32 order is part of what was
// measured.
void toTensor(const cv::Mat &rgb, std::vector<float> &out) {
  static const float kMean[3] = {0.485f, 0.456f, 0.406f};
  static const float kInvStd[3] = {1.0f / 0.229f, 1.0f / 0.224f,
                                   1.0f / 0.225f};
  const size_t plane = size_t(kCrop) * kCrop;
  out.resize(3 * plane);
  for (int y = 0; y < kCrop; ++y) {
    const uchar *src = rgb.ptr<uchar>(y);
    for (int x = 0; x < kCrop; ++x) {
      for (int c = 0; c < 3; ++c) {
        const float v = float(src[x * 3 + c]) / 255.0f;
        out[size_t(c) * plane + size_t(y) * kCrop + size_t(x)] =
            (v - kMean[c]) * kInvStd[c];
      }
    }
  }
}

} // namespace

struct Embedder::Impl {
  std::unique_ptr<Ort::Session> session;
  Ort::MemoryInfo memInfo =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::string inName;
  std::string outName;
};

Embedder::Embedder() : d_(std::make_unique<Impl>()) {}

Embedder::~Embedder() = default;

void Embedder::setError(const QString &message) {
  std::lock_guard<std::mutex> lock(errorMutex_);
  error_ = message;
}

bool Embedder::load(const QString &modelPath) {
  d_ = std::make_unique<Impl>();
  setError(QString());

  const QString path = modelPath.isEmpty() ? embedderModelPath() : modelPath;
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile()) {
    setError(QStringLiteral("model not found: %1").arg(path));
    return false;
  }
  if (info.size() <= (1 << 20)) {
    setError(QStringLiteral("%1 is only %2 bytes, so it is not the model")
                 .arg(path)
                 .arg(info.size()));
    return false;
  }

  try {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Intra-op threads are left at the runtime default (all cores) because the
    // builder decides how many images run concurrently and halves the core
    // count itself; setting it here as well would over-subscribe.
    d_->session = std::make_unique<Ort::Session>(
        ortEnv(), path.toStdString().c_str(), options);

    Ort::AllocatorWithDefaultOptions alloc;
    d_->inName = d_->session->GetInputNameAllocated(0, alloc).get();
    d_->outName = d_->session->GetOutputNameAllocated(0, alloc).get();
  } catch (const Ort::Exception &e) {
    setError(QString::fromUtf8(e.what()));
    d_->session.reset();
    return false;
  } catch (const std::exception &e) {
    setError(QString::fromUtf8(e.what()));
    d_->session.reset();
    return false;
  }
  return true;
}

bool Embedder::ready() const { return d_ && d_->session; }

bool Embedder::embedImage(const QImage &image, std::vector<float> &out) {
  if (!ready()) {
    setError(QStringLiteral("embedder not loaded"));
    return false;
  }
  const QImage rgb888 = image.convertToFormat(QImage::Format_RGB888);
  if (rgb888.isNull() || rgb888.width() < kCrop || rgb888.height() < kCrop) {
    setError(QStringLiteral("cannot decode image"));
    return false;
  }
  // Zero-copy view of the QImage rows; OpenCV is told the stride because
  // QImage pads scanlines and cv::Mat does not assume they match.
  const cv::Mat bgr(rgb888.height(), rgb888.width(), CV_8UC3,
                    const_cast<uchar *>(rgb888.constScanLine(0)),
                    rgb888.bytesPerLine());
  return embedBGR(bgr, out);
}

bool Embedder::embedFile(const QString &path, std::vector<float> &out) {
  if (!ready()) {
    setError(QStringLiteral("embedder not loaded"));
    return false;
  }
  // IMREAD_COLOR: 8-bit BGR, with EXIF orientation applied, which is what a
  // viewer shows and therefore what the model was measured on.
  const cv::Mat bgr = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
  if (bgr.empty()) {
    setError(QStringLiteral("cannot decode %1").arg(path));
    return false;
  }
  return embedBGR(bgr, out);
}

bool Embedder::embedBGR(const cv::Mat &bgr, std::vector<float> &out) {
  if (!ready()) {
    setError(QStringLiteral("embedder not loaded"));
    return false;
  }
  if (bgr.empty() || bgr.channels() != 3) {
    setError(QStringLiteral("not a colour image"));
    return false;
  }

  cv::Mat resized;
  const int shortest = std::min(bgr.cols, bgr.rows);
  if (shortest == kShortestEdge) {
    resized = bgr;
  } else {
    const double scale = double(kShortestEdge) / double(shortest);
    cv::resize(bgr, resized, cv::Size(), scale, scale, cv::INTER_CUBIC);
  }
  if (resized.cols < kCrop || resized.rows < kCrop) {
    setError(QStringLiteral("image too small after resize"));
    return false;
  }
  const cv::Mat crop =
      resized(cv::Rect((resized.cols - kCrop) / 2, (resized.rows - kCrop) / 2,
                       kCrop, kCrop));
  cv::Mat rgb;
  cv::cvtColor(crop, rgb, cv::COLOR_BGR2RGB);

  std::vector<float> input;
  toTensor(rgb, input);

  try {
    const std::array<int64_t, 4> shape{1, 3, kCrop, kCrop};
    Ort::Value in = Ort::Value::CreateTensor<float>(
        d_->memInfo, input.data(), input.size(), shape.data(), shape.size());
    const char *inNames[] = {d_->inName.c_str()};
    const char *outNames[] = {d_->outName.c_str()};
    const std::vector<Ort::Value> outs = d_->session->Run(
        Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);

    const std::vector<int64_t> outShape =
        outs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (outShape.size() != 3 || outShape[0] != 1 ||
        outShape[2] != kEmbeddingDim) {
      setError(QStringLiteral("unexpected output shape %1x%2x%3")
                   .arg(outShape.size() > 0 ? outShape[0] : -1)
                   .arg(outShape.size() > 1 ? outShape[1] : -1)
                   .arg(outShape.size() > 2 ? outShape[2] : -1));
      return false;
    }

    // last_hidden_state is [1, tokens, 384]; token 0 is the CLS token, which is
    // what the retrieval numbers were measured on.
    const float *data = outs[0].GetTensorData<float>();
    out.assign(data, data + kEmbeddingDim);

    double norm2 = 0.0;
    for (const float v : out)
      norm2 += double(v) * v;
    const double norm = std::sqrt(norm2);
    if (!(norm > 0.0)) {
      setError(QStringLiteral("degenerate embedding"));
      out.clear();
      return false;
    }
    for (float &v : out)
      v = float(double(v) / norm);
    return true;
  } catch (const Ort::Exception &e) {
    setError(QString::fromUtf8(e.what()));
    return false;
  } catch (const std::exception &e) {
    setError(QString::fromUtf8(e.what()));
    return false;
  }
}

EmbeddingRow Embedder::toRow(uint64_t fileHash, const std::vector<float> &vec) {
  EmbeddingRow row;
  row.fileHash = fileHash;
  if (vec.size() != size_t(kEmbeddingDim))
    return row;
  // Per-vector scale, so the quantisation uses the full int8 range whatever
  // the vector's magnitude: the components of a DINOv2 CLS vector peak around
  // 0.15, so a shared scale would spend the range badly. The scale itself is not
  // stored, because it is recoverable -- ImageIndex::search divides the dot
  // product by the row's own length, which cancels it exactly.
  float peak = 0.0f;
  for (const float v : vec)
    peak = std::max(peak, std::abs(v));
  if (peak <= 0.0f)
    return row;
  for (int i = 0; i < kEmbeddingDim; ++i) {
    const float scaled = vec[size_t(i)] * (127.0f / peak);
    row.vec[size_t(i)] =
        int8_t(std::max(-127.0f, std::min(127.0f, std::round(scaled))));
  }
  return row;
}

Embedder &sharedEmbedder() {
  static std::mutex mutex;
  static std::unique_ptr<Embedder> instance;

  std::lock_guard<std::mutex> lock(mutex);
  if (!instance)
    instance = std::make_unique<Embedder>();
  // Retried whenever the model is not yet there, so a user who installs the
  // weights after first launch does not have to restart the app.
  if (!instance->ready() && embedderModelPresent())
    instance->load();
  return *instance;
}

#else // !LUCIDGRASP_HAVE_ORT

// Built without ONNX Runtime. The class is still defined so that no caller
// needs an #ifdef, but it can never load a session, so every embedding call
// fails with an explanation rather than returning something plausible.
struct Embedder::Impl {};

Embedder::Embedder() = default;
Embedder::~Embedder() = default;

void Embedder::setError(const QString &message) { error_ = message; }

bool Embedder::load(const QString &) {
  setError(QStringLiteral("built without ONNX Runtime"));
  return false;
}

bool Embedder::ready() const { return false; }

bool Embedder::embedFile(const QString &, std::vector<float> &out) {
  setError(QStringLiteral("built without ONNX Runtime"));
  out.clear();
  return false;
}

bool Embedder::embedImage(const QImage &, std::vector<float> &out) {
  setError(QStringLiteral("built without ONNX Runtime"));
  out.clear();
  return false;
}

EmbeddingRow Embedder::toRow(uint64_t, const std::vector<float> &) {
  return EmbeddingRow();
}

Embedder &sharedEmbedder() {
  static Embedder instance;
  return instance;
}

#endif

} // namespace core
