#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QSize>
#include <QString>

namespace core {

struct ClipEmbedderImpl;

class ClipEmbedder {
public:
  ClipEmbedder();
  ~ClipEmbedder();
  ClipEmbedder(const ClipEmbedder &) = delete;
  ClipEmbedder &operator=(const ClipEmbedder &) = delete;
  ClipEmbedder(ClipEmbedder &&) noexcept;
  ClipEmbedder &operator=(ClipEmbedder &&) noexcept;
  bool load(const QString &modelPath);
  bool loaded() const { return impl_ != nullptr; }
  int dim() const;
  bool embed(const QImage &image, std::vector<float> &out) const;

private:
  std::unique_ptr<ClipEmbedderImpl> impl_;
};
double cosineSimilarity(const std::vector<float> &a,
                        const std::vector<float> &b);

} // namespace core