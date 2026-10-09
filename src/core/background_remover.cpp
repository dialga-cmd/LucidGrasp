#include "core/background_remover.h"

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
#include <QImageReader>
#include <QImageIOHandler>
#include <QSize>
#include <QStandardPaths>

#include <onnxruntime_cxx_api.h>

namespace core {

namespace {

constexpr int kDefaultSize = 1024;

const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kStd[3] = {0.229f, 0.224f, 0.225f};

Ort::Env &onnxEnv()
{
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "lucidgrasp");
  return env;
}

cv::Mat toRgbMat(const QImage &image)
{
  const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
  return cv::Mat(rgb.height(), rgb.width(), CV_8UC3,
                 static_cast<void *>(const_cast<uchar *>(rgb.constBits())),
                 rgb.bytesPerLine())
      .clone();
}

QString errorMessage(const std::exception &e)
{
  return QString::fromUtf8(e.what());
}

QString candidateModelPath()
{
  const QString fileName = modelFileName();
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

}  // namespace

BackgroundRemover::BackgroundRemover() = default;

BackgroundRemover::~BackgroundRemover() = default;

bool BackgroundRemover::loadModel(const QString &modelPath, QString *error)
{
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.DisableCpuMemArena();
    session_ = std::make_unique<Ort::Session>(
        onnxEnv(), modelPath.toStdString().c_str(), options);

    Ort::AllocatorWithDefaultOptions allocator;

    const Ort::TypeInfo inputInfo = session_->GetInputTypeInfo(0);
    const std::vector<int64_t> shape =
        inputInfo.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() >= 4) {
      inputHeight_ = shape[2] > 0 ? static_cast<int>(shape[2]) : kDefaultSize;
      inputWidth_ = shape[3] > 0 ? static_cast<int>(shape[3]) : kDefaultSize;
    } else {
      inputHeight_ = kDefaultSize;
      inputWidth_ = kDefaultSize;
    }

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

bool BackgroundRemover::isLoaded() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return session_ != nullptr;
}

QString BackgroundRemover::modelPath() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return modelPath_;
}

bool BackgroundRemover::loadInput(const QImage &image, int modelSize,
                                  std::vector<float> *tensor)
{
  if (modelSize <= 0 || image.isNull() || tensor == nullptr)
    return false;

  const cv::Mat rgb = toRgbMat(image);
  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(modelSize, modelSize), 0, 0,
             cv::INTER_LANCZOS4);

  cv::Mat f;
  resized.convertTo(f, CV_32FC3, 1.0 / 255.0);

  double maxValue = 0.0;
  cv::minMaxLoc(f, nullptr, &maxValue);
  if (!std::isfinite(static_cast<float>(maxValue)) || maxValue <= 0.0)
    return false;
  const float scale = static_cast<float>(maxValue);

  std::vector<cv::Mat> planes;
  cv::split(f, planes);

  tensor->resize(size_t(3) * modelSize * modelSize);
  for (int c = 0; c < 3; ++c) {
    const cv::Mat &plane = planes[c];
    const float *src = plane.ptr<float>(0);
    const size_t per = plane.total();
    const float mean = kMean[c];
    const float denom = kStd[c] * scale;
    for (size_t i = 0; i < per; ++i)
      (*tensor)[c * per + i] = (src[i] - mean * scale) / denom;
  }
  return true;
}

bool BackgroundRemover::removeBackground(const QImage &image, QImage *cutout,
                                         QString *error)
{
  if (image.isNull() || cutout == nullptr) {
    if (error)
      *error = QStringLiteral("invalid input");
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!session_) {
    if (error)
      *error = QStringLiteral("no background model loaded");
    return false;
  }

  try {
    const int h = inputHeight_;
    const int w = inputWidth_;

    std::vector<float> tensor;
    if (!loadInput(image, h, &tensor)) {
      if (error)
        *error = QStringLiteral("could not preprocess the image");
      return false;
    }

    const int64_t shape[4] = {1, 3, h, w};
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
    if (outShape.size() < 2) {
      if (error)
        *error = QStringLiteral("unexpected model output shape");
      return false;
    }
    const int64_t outH = outShape.size() > 2 ? outShape[2] : 1;
    const int64_t outW = outShape.size() > 3 ? outShape[3] : 1;
    if (outH <= 0 || outW <= 0) {
      if (error)
        *error = QStringLiteral("unexpected model output size");
      return false;
    }

    const float *raw = output.GetTensorData<float>();
    cv::Mat pred(static_cast<int>(outH), static_cast<int>(outW), CV_32F);
    float minValue = std::numeric_limits<float>::max();
    float maxValue = std::numeric_limits<float>::lowest();
    const size_t per = size_t(outH) * size_t(outW);
    for (size_t i = 0; i < per; ++i) {
      const float v = 1.0f / (1.0f + std::exp(-raw[i]));
      pred.ptr<float>(0)[i] = v;
      minValue = std::min(minValue, v);
      maxValue = std::max(maxValue, v);
    }

    if (maxValue - minValue > 1e-6f) {
      const float range = maxValue - minValue;
      for (size_t i = 0; i < per; ++i)
        pred.ptr<float>(0)[i] = (pred.ptr<float>(0)[i] - minValue) / range;
    }

    cv::Mat alphaF;
    cv::resize(pred, alphaF, cv::Size(image.width(), image.height()), 0, 0,
               cv::INTER_LANCZOS4);

    QImage result(image.width(), image.height(), QImage::Format_RGBA8888);
    const QImage rgbSource = image.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < image.height(); ++y) {
      const uchar *src = rgbSource.constScanLine(y);
      const float *alpha = alphaF.ptr<float>(y);
      uchar *dst = result.scanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        const float a = std::min(1.0f, std::max(0.0f, alpha[x]));
        dst[x * 4] = src[x * 3];
        dst[x * 4 + 1] = src[x * 3 + 1];
        dst[x * 4 + 2] = src[x * 3 + 2];
        dst[x * 4 + 3] = static_cast<uchar>(std::lround(a * 255.0f));
      }
    }

    *cutout = std::move(result);
    return true;
  } catch (const std::bad_alloc &) {
    if (error)
      *error = QStringLiteral(
          "not enough memory to run the background model; close other "
          "programs or switch to the smaller lite model");
    return false;
  } catch (const std::exception &e) {
    if (error)
      *error = errorMessage(e);
    return false;
  }
}

QImage loadImageForBackground(const QString &path)
{
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  const bool huge =
      size.isValid() && (size.width() > kBackgroundSourceMaxDim ||
                         size.height() > kBackgroundSourceMaxDim);
  if (!huge)
    return reader.read();
  const QSize target =
      size.scaled(kBackgroundSourceMaxDim, kBackgroundSourceMaxDim,
                  Qt::KeepAspectRatio);
  if (reader.supportsOption(QImageIOHandler::ScaledSize)) {
    // JPEG and friends decode directly at the targets size, staying far
    // below the allocation guard.
    reader.setScaledSize(target);
    return reader.read();
  }
  // PNG and other handlers without native scaled decoding allocate the full
  // image first, so the default guard would reject them before any
  // downscaling; raise the guard for the read and scale toward the target.
  // The full-size buffer is transient and immediately scaled down.
  const int previous = QImageReader::allocationLimit();
  QImageReader::setAllocationLimit(1024);
  QImageReader full(path);
  full.setAutoTransform(true);
  full.setScaledSize(target);
  QImage image = full.read();
  QImageReader::setAllocationLimit(previous);
  return image;
}

QString modelFileName()
{
  return QStringLiteral("birefnet-general.onnx");
}

QString defaultModelPath()
{
  const QByteArray fromEnv =
      qgetenv("LUCIDGRASP_BG_MODEL");
  if (!fromEnv.isEmpty()) {
    const QString path = QString::fromLocal8Bit(fromEnv);
    if (QFileInfo::exists(path))
      return path;
  }
  return candidateModelPath();
}

}  // namespace core