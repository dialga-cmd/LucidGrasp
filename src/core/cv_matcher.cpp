#include "cv_matcher.h"
#include <opencv2/opencv.hpp>
#include <QImage>

namespace core {

static cv::Mat qimageToMat(const QImage &image) {
    QImage img = image.convertToFormat(QImage::Format_RGB888);
    return cv::Mat(img.height(), img.width(), CV_8UC3, (void*)img.constBits(), img.bytesPerLine()).clone();
}

// Compute SSIM between two grayscale images (returns value between 0.0 and 1.0).
// This measures structural patterns, luminance, and contrast while being
// completely blind to color grading, tinting, or hue shifts.
static double computeSSIM(const cv::Mat &gray1, const cv::Mat &gray2) {
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

    cv::Scalar mean_ssim = cv::mean(ssim_map);
    return std::max(0.0, mean_ssim[0]);
}

double CvMatcher::match(const QImage &queryImage, const QImage &dbImage) const {
    if (queryImage.isNull() || dbImage.isNull()) return 0.0;

    cv::Mat mat1 = qimageToMat(queryImage);
    cv::Mat mat2 = qimageToMat(dbImage);

    // Resize both to a common size so SSIM and histograms are comparable
    // regardless of resolution differences between raw and edited versions.
    cv::Size common(256, 256);
    cv::Mat r1, r2;
    cv::resize(mat1, r1, common, 0, 0, cv::INTER_AREA);
    cv::resize(mat2, r2, common, 0, 0, cv::INTER_AREA);

    // --- 1. STRUCTURAL SIMILARITY (SSIM) on grayscale ---
    // Converts to grayscale first, so color grading, tinting, and hue shifts
    // are completely ignored. Only the shapes, edges, contrast, and luminance
    // patterns matter. A raw photo and its color-graded edit will score very
    // high here because the structure is identical.
    cv::Mat gray1, gray2;
    cv::cvtColor(r1, gray1, cv::COLOR_RGB2GRAY);
    cv::cvtColor(r2, gray2, cv::COLOR_RGB2GRAY);
    double ssimScore = computeSSIM(gray1, gray2);

    // --- 2. STRUCTURAL FEATURE MATCHING (ORB Keypoints) ---
    // ORB works on intensity gradients, not colors, so it is also robust
    // to color grading. It catches structural matches even when images
    // have been cropped or slightly rotated.
    cv::Ptr<cv::ORB> orb = cv::ORB::create(500);
    std::vector<cv::KeyPoint> keypoints1, keypoints2;
    cv::Mat descriptors1, descriptors2;

    orb->detectAndCompute(r1, cv::noArray(), keypoints1, descriptors1);
    orb->detectAndCompute(r2, cv::noArray(), keypoints2, descriptors2);

    double orbScore = 0.0;
    if (!descriptors1.empty() && !descriptors2.empty()) {
        cv::BFMatcher matcher(cv::NORM_HAMMING);
        std::vector<std::vector<cv::DMatch>> knn_matches;
        matcher.knnMatch(descriptors1, descriptors2, knn_matches, 2);

        int good_matches = 0;
        for (size_t i = 0; i < knn_matches.size(); i++) {
            if (knn_matches[i].size() > 1) {
                if (knn_matches[i][0].distance < 0.75f * knn_matches[i][1].distance) {
                    good_matches++;
                }
            } else if (knn_matches[i].size() == 1) {
                good_matches++;
            }
        }

        int min_kpts = std::min(keypoints1.size(), keypoints2.size());
        if (min_kpts > 0) {
            orbScore = std::min(1.0, ((double)good_matches / min_kpts) * 4.0);
        }
    }

    // --- 3. PIXEL COLOR DISTRIBUTION (Histogram Intersection) ---
    // Still useful for catching images that share the same content AND the
    // same color palette. But it no longer dominates the score, so a color
    // grade won't destroy the match.
    cv::Mat hsv1, hsv2;
    cv::cvtColor(r1, hsv1, cv::COLOR_RGB2HSV);
    cv::cvtColor(r2, hsv2, cv::COLOR_RGB2HSV);

    int h_bins = 50, s_bins = 60;
    int histSize[] = {h_bins, s_bins};
    float h_ranges[] = { 0, 180 };
    float s_ranges[] = { 0, 256 };
    const float* ranges[] = { h_ranges, s_ranges };
    int channels[] = { 0, 1 };

    cv::Mat hist1, hist2;
    cv::calcHist(&hsv1, 1, channels, cv::Mat(), hist1, 2, histSize, ranges, true, false);
    cv::normalize(hist1, hist1, 1.0, 0.0, cv::NORM_L1);

    cv::calcHist(&hsv2, 1, channels, cv::Mat(), hist2, 2, histSize, ranges, true, false);
    cv::normalize(hist2, hist2, 1.0, 0.0, cv::NORM_L1);

    double colorScore = cv::compareHist(hist1, hist2, cv::HISTCMP_INTERSECT);

    // Final score:
    //   45% SSIM (color-blind structural comparison, catches edited versions)
    //   35% ORB  (keypoint matching, catches crops and rotations)
    //   20% Color histogram (pixel color distribution, boosts exact matches)
    return (0.45 * ssimScore) + (0.35 * orbScore) + (0.20 * colorScore);
}

} // namespace core
