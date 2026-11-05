#pragma once

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

#include <vector>

#include <QImage>

namespace core {

class CvMatcher {
public:
  struct Prepared {
    cv::Mat gray;
    cv::Mat hist;
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    bool valid = false;
  };

  CvMatcher();
  ~CvMatcher();

  CvMatcher(const CvMatcher &) = delete;
  CvMatcher &operator=(const CvMatcher &) = delete;

  Prepared prepare(const QImage &image);

  double match(const Prepared &a, const Prepared &b) const;

private:
  cv::Ptr<cv::ORB> orb_;
};

}
