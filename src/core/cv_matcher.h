#pragma once

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

#include <vector>

#include <QImage>

namespace core {

// Compares two images with three independent signals: SSIM, ORB keypoint
// matching, and HSV histogram intersection.
//
// Turning an image into that form is the expensive half of a comparison, so
// the two halves are split: prepare() runs once for the query and once per
// candidate, and match() then compares two prepared images cheaply. Doing it
// this way means the query is decoded, resized, and run through ORB a single
// time per search instead of once per indexed image.
class CvMatcher {
public:
  struct Prepared {
    cv::Mat gray;         // 256x256 CV_8UC1
    cv::Mat hist;         // L1-normalised 50x60 HSV histogram
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;  // ORB binary descriptors
    bool valid = false;
  };

  CvMatcher();
  ~CvMatcher();

  CvMatcher(const CvMatcher &) = delete;
  CvMatcher &operator=(const CvMatcher &) = delete;

  // Build the reusable form of an image. Yields an invalid Prepared when the
  // image is null or fails to decode, which match() scores as 0.0.
  Prepared prepare(const QImage &image);

  // Similarity in [0.0, 1.0].
  double match(const Prepared &a, const Prepared &b) const;

private:
  // Built once per matcher and reused, since ORB::create is not cheap.
  cv::Ptr<cv::ORB> orb_;
};

} // namespace core
