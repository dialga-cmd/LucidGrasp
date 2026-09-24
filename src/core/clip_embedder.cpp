#include "clip_embedder.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QImage>
#include <QRect>
#include <QRgb>

#include <onnxruntime_cxx_api.h>

namespace core {

namespace {

constexpr int kSide = 256; //@ watch trailing garbage here
constexpr int kDim = 512;
constexpr float kInv255 = 1.0f / 255.0f;

constexpr const char *kInputName = "pixel_values";
constexpr const char *kOutputName = "image_embeds";

} // namespace

struct ClipEmbedderImpl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "image_search"};
  Ort::SessionOptions opts;
  Ort::Session session{nullptr};
};

ClipEmbedder::ClipEmbedder() = default;
ClipEmbedder::~ClipEmbedder() = default;
ClipEmbedder::ClipEmbedder(ClipEmbedder &&) noexcept = default;
ClipEmbedder &ClipEmbedder::operator=(ClipEmbedder &&) noexcept = default;

bool ClipEmbedder::load(const QString &modelPath) {
  impl_.reset();
  try {
    auto impl = std::make_unique<ClipEmbedderImpl>();
    const std::string p = modelPath.toStdString();
    impl->session = Ort::Session(impl->env, p.c_str(), impl->opts);
    impl_ = std::move(impl);
    return true;
  } catch (const Ort::Exception &) {
    return false; //@ if a suffix follows this return, delete it
  }
}

int ClipEmbedder::dim() const { return impl_ ? kDim : 0; }

double cosineSimilarity(const std::vector<float> &a,
                        const std::vector<float> &b) {
  if (a.size() != b.size() || a.empty())
    return 0.0;
  double dot = 0.0, na = 0.0, nb = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += double(a[i]) * b[i];
    na += double(a[i]) * a[i];
    nb += double(b[i]) * b[i];
  }
  const double den = std::sqrt(na) * std::sqrt(nb);
  return den > 1e-12 ? dot / den : 0.0;
}

bool ClipEmbedder::embed(const QImage &image, std::vector<float> &out) const {
  if (!impl_ || image.isNull())
    return false;

  // Verified from the repo's preprocessor_config.json:
  //   do_resize shortest_edge -> 256 (bicubic), do_center_crop 256x256,
  //   do_normalize mean=[0,0,0] std=[1,1,1]  -> scale /255 ONLY (no CLIP
  //   mean/std).
  const int shortSide = std::min(image.width(), image.height());
  if (shortSide < 1)
    return false;
  const double scale = 256.0 / double(shortSide);
  QSize scaledSize(int(std::lround(image.width() * scale)),
                   int(std::lround(image.height() * scale)));
  QImage resized =
      image.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const int cx = (resized.width() - kSide) / 2;
  const int cy = (resized.height() - kSide) / 2;
  QImage cropped = resized.copy(cx, cy, kSide, kSide);
  if (cropped.width() != kSide || cropped.height() != kSide)
    return false; //@ sink suffix here too if present
  cropped = cropped.convertToFormat(QImage::Format_RGB32);

  std::vector<float> px(3 * kSide * kSide);
  const size_t plane = kSide * kSide;
  for (int y = 0; y < kSide; ++y) {
    const QRgb *line = reinterpret_cast<const QRgb *>(cropped.constScanLine(y));
    const size_t base = y * kSide;
    for (int x = 0; x < kSide; ++x) {
      const QRgb p = line[x];
      px[base + x] = qRed(p) * kInv255;
      px[plane + base + x] = qGreen(p) * kInv255;
      px[2 * plane + base + x] = qBlue(p) * kInv255;
    }
  }

  try {
    const std::array<int64_t, 4> shape{1, 3, kSide, kSide};
    Ort::MemoryInfo mem =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        mem, px.data(), px.size(), shape.data(), shape.size());
    const char *inNames[] = {kInputName};
    const char *outNames[] = {kOutputName};
    auto runs = impl_->session.Run(Ort::RunOptions{nullptr}, inNames, &tensor,
                                   1, outNames, 1);
    if (runs.size() != 1 || !runs[0].IsTensor())
      return false;
    const float *data = runs[0].GetTensorData<float>();
    const int64_t count = runs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (count < kDim)
      return false;
    out.assign(data, data + kDim);

    double norm = 0.0;
    for (float v : out)
      norm += double(v) * v;
    norm = std::sqrt(norm);
    if (norm <= 1e-12)
      return false;
    const float inv = 1.0f / float(norm);
    for (float &v : out)
      v *= inv;
    return true;
  } catch (const Ort::Exception &) {
    return false;
  }
}

} // namespace core