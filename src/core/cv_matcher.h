#pragma once
#include <QImage>

namespace core {
class CvMatcher {
public:
  CvMatcher() = default;
  ~CvMatcher() = default;
  double match(const QImage &queryImage, const QImage &dbImage) const;
};
} // namespace core
