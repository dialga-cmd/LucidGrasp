#pragma once

#include <array>
#include <cstdint>

#include <QImage>
#include <QString>

namespace core {

inline constexpr int kHistBins = 256; // 16 hue x 16 saturation

struct Features {
    uint64_t fileHash = 0;
    uint64_t phash = 0;
    uint64_t dhash = 0;
    // File state the hashes above were computed from. A negative size means
    // unknown, which is what a query image carries; queries are never
    // stale-checked because they are read fresh on every search.
    qint64 size = -1;
    qint64 mtimeMs = 0;
    std::array<uint8_t, kHistBins> hist{};
};

uint64_t fileHash(const QString& path);

// Decodes an image for hashing or comparison, asking the image plugin to
// shrink it on the way in. Decoding first and resizing afterwards is what makes
// a large photograph expensive: a 12 MP JPEG becomes a 36 MB QImage that is
// then copied again into a cv::Mat before being squeezed down to the 256 px the
// comparison actually reads. This also applies EXIF orientation, so the pixels
// that get hashed are the pixels a viewer shows.
QImage loadScaled(const QString& path, int maxSide);

uint64_t computePHash(const QImage& image);
uint64_t computeDHash(const QImage& image);
std::array<uint8_t, kHistBins> computeHist(const QImage& image);

bool extractFeatures(const QString& path, Features& out,
                     QImage* loadedImage = nullptr);

double hammingSimilarity(uint64_t a, uint64_t b);
double histIntersection(const std::array<uint8_t, kHistBins>& a,
                        const std::array<uint8_t, kHistBins>& b);
bool isExactMatch(const Features& a, const Features& b);

// True when the file changed since these features were computed, so the stored
// hashes no longer describe what is on disk.
bool isStale(const Features& f, const QString& path);

// Cheap in-memory ranking signal used to shortlist candidates before the
// expensive OpenCV comparison runs. It reads only the stored hashes, so it
// costs no disk I/O and stays usable at whole-filesystem scale. Weights favour
// the grayscale hashes because those are the signals that survive the color
// grading this tool is built to tolerate.
double prefilterScore(const Features& query, const Features& entry);

} // namespace core
