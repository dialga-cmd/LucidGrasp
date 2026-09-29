#include "cv_matcher.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace core {

namespace {

constexpr int kCompareSide = 256;
constexpr int kHueBins = 50;
constexpr int kSatBins = 60;
constexpr int kMaxKeypoints = 500;

cv::Mat toMat(const QImage &image) {
  QImage img = image.convertToFormat(QImage::Format_RGB888);
  return cv::Mat(img.height(), img.width(), CV_8UC3, (void *)img.constBits(),
                 img.bytesPerLine())
      .clone();
}

// Compare two grayscale images by luminance, contrast, and spatial structure.
// Blind to color grading, tinting, and hue shifts, which is what lets a raw
// photo and its graded edit still match.
double computeSSIM(const cv::Mat &gray1, const cv::Mat &gray2) {
  cv::Mat g1, g2;
  gray1.convertTo(g1, CV_64F);
  gray2.convertTo(g2, CV_64F);

  cv::Mat g1_sq = g1.mul(g1);
  cv::Mat g2_sq = g2.mul(g2);
  cv::Mat g1_g2 = g1.mul(g2);

  cv::Mat mu1, mu2;
  cv::GaussianBlur(g1, mu1, cv::Size(11, 11), 1.5);
  cv::GaussianBlur(g2, mu2, cv::Size(11, 11), 1.5);

  cv::Mat mu1_sq = mu1.mul(mu1);
  cv::Mat mu2_sq = mu2.mul(mu2);
  cv::Mat mu1_mu2 = mu1.mul(mu2);

  cv::Mat sigma1_sq, sigma2_sq, sigma12;
  cv::GaussianBlur(g1_sq, sigma1_sq, cv::Size(11, 11), 1.5);
  sigma1_sq -= mu1_sq;
  cv::GaussianBlur(g2_sq, sigma2_sq, cv::Size(11, 11), 1.5);
  sigma2_sq -= mu2_sq;
  cv::GaussianBlur(g1_g2, sigma12, cv::Size(11, 11), 1.5);
  sigma12 -= mu1_mu2;

  const double C1 = 6.5025;   // (0.01 * 255)^2
  const double C2 = 58.5225;  // (0.03 * 255)^2

  cv::Mat numerator = (2.0 * mu1_mu2 + C1).mul(2.0 * sigma12 + C2);
  cv::Mat denominator = (mu1_sq + mu2_sq + C1).mul(sigma1_sq + sigma2_sq + C2);

  cv::Mat ssim_map;
  cv::divide(numerator, denominator, ssim_map);

  const cv::Scalar mean_ssim = cv::mean(ssim_map);
  return std::max(0.0, mean_ssim[0]);
}

// ORB works on intensity gradients, so it survives color grading and still
// matches images that were cropped or slightly rotated. Lowe's ratio test
// discards ambiguous matches that would otherwise inflate the score.
double orbSimilarity(const std::vector<cv::KeyPoint> &k1, const cv::Mat &d1,
                     const std::vector<cv::KeyPoint> &k2, const cv::Mat &d2) {
  if (d1.empty() || d2.empty())
    return 0.0;

  cv::BFMatcher matcher(cv::NORM_HAMMING);
  std::vector<std::vector<cv::DMatch>> knn_matches;
  matcher.knnMatch(d1, d2, knn_matches, 2);

  int good_matches = 0;
  for (const std::vector<cv::DMatch> &m : knn_matches) {
    if (m.size() > 1) {
      if (m[0].distance < 0.75f * m[1].distance)
        ++good_matches;
    } else if (m.size() == 1) {
      ++good_matches;
    }
  }

  const size_t min_kpts = std::min(k1.size(), k2.size());
  if (min_kpts == 0)
    return 0.0;
  return std::min(1.0, (double(good_matches) / double(min_kpts)) * 4.0);
}

} // namespace

CvMatcher::CvMatcher() : orb_(cv::ORB::create(kMaxKeypoints)) {}

CvMatcher::~CvMatcher() = default;

CvMatcher::Prepared CvMatcher::prepare(const QImage &image) {
  Prepared p;
  if (image.isNull())
    return p;

  // Resize to a common size so SSIM and histograms stay comparable regardless
  // of the resolution difference between an original and its edited version.
  cv::Mat resized;
  cv::resize(toMat(image), resized, cv::Size(kCompareSide, kCompareSide), 0, 0,
             cv::INTER_AREA);

  cv::cvtColor(resized, p.gray, cv::COLOR_RGB2GRAY);

  cv::Mat hsv;
  cv::cvtColor(resized, hsv, cv::COLOR_RGB2HSV);
  const int hist_size[] = {kHueBins, kSatBins};
  const float h_range[] = {0, 180};
  const float s_range[] = {0, 256};
  const float *ranges[] = {h_range, s_range};
  const int channels[] = {0, 1};
  cv::calcHist(&hsv, 1, channels, cv::Mat(), p.hist, 2, hist_size, ranges, true,
               false);
  cv::normalize(p.hist, p.hist, 1.0, 0.0, cv::NORM_L1);

  orb_->detectAndCompute(resized, cv::noArray(), p.keypoints, p.descriptors);
  p.valid = true;
  return p;
}

double CvMatcher::match(const Prepared &a, const Prepared &b) const {
  if (!a.valid || !b.valid)
    return 0.0;

  const double ssim = computeSSIM(a.gray, b.gray);
  const double hist = cv::compareHist(a.hist, b.hist, cv::HISTCMP_INTERSECT);
  const double orb = orbSimilarity(a.keypoints, a.descriptors, b.keypoints,
                                   b.descriptors);

  // When neither image yields a single keypoint, ORB did not run: its share is
  // silence, not evidence of dissimilarity. Scoring it as 0.0 caps a
  // byte-identical pair of smooth images at 0.72 (0.8 * 0.65 + 0.2) while the
  // same pair with texture reaches 1.0, so the user's threshold would mean
  // different things on different libraries. Renormalise the two signals that
  // actually ran onto the full range in that case. The prefilter always has a
  // keypoint-free parallel: pHash/dHash are gradient-free on such images too,
  // so there is no hidden keypoint term to double-count.
  if (a.keypoints.empty() && b.keypoints.empty())
    return (0.45 * ssim + 0.20 * hist) / 0.65;

  // 45% SSIM (color-blind structural comparison, catches edited versions)
  // 35% ORB  (keypoint matching, catches crops and rotations)
  // 20% color histogram (pixel color distribution, boosts exact matches)
  return 0.45 * ssim + 0.35 * orb + 0.20 * hist;
}

} // namespace core
