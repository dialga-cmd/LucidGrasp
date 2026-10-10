#include "core/semantic_embedder.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <onnxruntime_cxx_api.h>

namespace core {

namespace {

const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kStd[3] = {0.229f, 0.224f, 0.225f};

Ort::Env &onnxEnv()
{
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "lucidgrasp");
  return env;
}

QString errorMessage(const std::exception &e)
{
  return QString::fromUtf8(e.what());
}

QString candidateModelPath()
{
  const QString fileName = semanticModelFileName();
  const QStringList dirs{
      QCoreApplication::applicationDirPath() + QStringLiteral("/models"),
      QDir::currentPath() + QStringLiteral("/models"),
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
          QStringLiteral("/models"),
      QStringLiteral("/usr/share/lucidgrasp/models"),
  };
  for (const QString &dir : dirs) {
    const QString candidate = dir + QLatin1Char('/') + fileName;
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return QString();
}

// ONNX Runtime names its path type ORTCHAR_T: a wide string on Windows and a
// narrow one elsewhere. Convert the Qt path to whichever the platform wants
// before handing it to the session.
std::unique_ptr<Ort::Session> makeSession(Ort::Env &env,
                                          const QString &modelPath,
                                          const Ort::SessionOptions &options)
{
#ifdef _WIN32
  const std::wstring native = modelPath.toStdWString();
#else
  const std::string native = modelPath.toStdString();
#endif
  return std::make_unique<Ort::Session>(env, native.c_str(), options);
}

}  // namespace

SemanticEmbedder::SemanticEmbedder() = default;

SemanticEmbedder::~SemanticEmbedder() = default;

void SemanticEmbedder::unload()
{
  std::lock_guard<std::mutex> lock(mutex_);
  session_.reset();
  modelPath_.clear();
}

bool SemanticEmbedder::loadModel(const QString &modelPath, QString *error)
{
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.DisableCpuMemArena();
    session_ = makeSession(onnxEnv(), modelPath, options);

    Ort::AllocatorWithDefaultOptions allocator;
    inputName_ =
        std::string(session_->GetInputNameAllocated(0, allocator).get());
    outputName_ =
        std::string(session_->GetOutputNameAllocated(0, allocator).get());

    modelPath_ = modelPath;
    return true;
  } catch (const std::bad_alloc &) {
    session_.reset();
    modelPath_.clear();
    if (error)
      *error = QStringLiteral("not enough memory to load the model");
    return false;
  } catch (const std::exception &e) {
    session_.reset();
    modelPath_.clear();
    if (error)
      *error = errorMessage(e);
    return false;
  }
}

bool SemanticEmbedder::isLoaded() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return session_ != nullptr;
}

QString SemanticEmbedder::modelPath() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return modelPath_;
}

bool SemanticEmbedder::loadInput(const QImage &image, std::vector<float> *tensor)
{
  if (image.isNull() || tensor == nullptr)
    return false;

  const int size = kInputSize;
  const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
  cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
               static_cast<void *>(const_cast<uchar *>(rgb.constBits())),
               rgb.bytesPerLine());
  cv::Mat src;
  view.copyTo(src);
  if (src.empty())
    return false;

  // Resize so the shorter side equals the model input, then center-crop the
  // square the model expects.
  const double scale =
      double(size) / std::min(src.cols, src.rows);
  const int nw = std::max(int(std::lround(double(src.cols) * scale)), size);
  const int nh = std::max(int(std::lround(double(src.rows) * scale)), size);

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(nw, nh), 0, 0, cv::INTER_CUBIC);

  const int x0 = (nw - size) / 2;
  const int y0 = (nh - size) / 2;
  const cv::Mat crop = resized(cv::Rect(x0, y0, size, size));

  cv::Mat f;
  crop.convertTo(f, CV_32FC3, 1.0 / 255.0);

  const size_t per = size_t(size) * size;
  tensor->resize(size_t(3) * per);

  std::vector<cv::Mat> planes;
  cv::split(f, planes);
  for (int c = 0; c < 3; ++c) {
    const cv::Mat &plane = planes[c];
    const float *srcPixels = plane.ptr<float>(0);
    const float mean = kMean[c];
    const float denom = kStd[c];
    float *dst = tensor->data() + c * per;
    for (size_t i = 0; i < per; ++i)
      dst[i] = (srcPixels[i] - mean) / denom;
  }
  return true;
}

bool SemanticEmbedder::embed(const QImage &image, std::vector<float> *out,
                             QString *error) const
{
  if (image.isNull() || out == nullptr) {
    if (error)
      *error = QStringLiteral("invalid input");
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!session_) {
    if (error)
      *error = QStringLiteral("no semantic model loaded");
    return false;
  }

  try {
    std::vector<float> tensor;
    if (!loadInput(image, &tensor)) {
      if (error)
        *error = QStringLiteral("could not preprocess the image");
      return false;
    }

    const int64_t shape[4] = {1, 3, kInputSize, kInputSize};
    const Ort::MemoryInfo memory =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(
        memory, tensor.data(), tensor.size(), shape, 4);

    const std::array<const char *, 1> inputNames{inputName_.c_str()};
    const std::array<const char *, 1> outputNames{outputName_.c_str()};
    const std::array<Ort::Value, 1> inputs{std::move(input)};

    std::vector<Ort::Value> outputs = session_->Run(
        Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(),
        inputs.size(), outputNames.data(), outputNames.size());

    const Ort::Value &output = outputs.front();
    const std::vector<int64_t> outShape =
        output.GetTensorTypeAndShapeInfo().GetShape();
    if (outShape.size() < 3 || outShape[2] <= 0) {
      if (error)
        *error = QStringLiteral("unexpected model output shape");
      return false;
    }

    // The first token is the CLS token that summarizes the whole image.
    const size_t dim = size_t(outShape[2]);
    const float *raw = output.GetTensorData<float>();

    out->resize(dim);
    double sumSq = 0.0;
    for (size_t i = 0; i < dim; ++i) {
      (*out)[i] = raw[i];
      sumSq += double(raw[i]) * double(raw[i]);
    }
    if (!(sumSq > 0.0) || !std::isfinite(sumSq)) {
      out->clear();
      if (error)
        *error = QStringLiteral("degenerate embedding produced");
      return false;
    }

    const float inv = float(1.0 / std::sqrt(sumSq));
    for (size_t i = 0; i < dim; ++i)
      (*out)[i] *= inv;
    return true;
  } catch (const std::bad_alloc &) {
    if (error)
      *error = QStringLiteral("not enough memory to run the semantic model");
    return false;
  } catch (const std::exception &e) {
    if (error)
      *error = errorMessage(e);
    return false;
  }
}

QString semanticModelFileName()
{
  return QStringLiteral("dinov2_small_int8.onnx");
}

QString defaultSemanticModelPath()
{
  const QByteArray fromEnv = qgetenv("LUCIDGRASP_SEMANTIC_MODEL");
  if (!fromEnv.isEmpty()) {
    const QString path = QString::fromLocal8Bit(fromEnv);
    if (QFileInfo::exists(path))
      return path;
  }
  return candidateModelPath();
}

}  // namespace core