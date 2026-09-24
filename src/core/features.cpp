#include "core/features.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QColor>
#include <QFile>

namespace core {

namespace {

constexpr int kDctN = 32;
constexpr int kHashSide = 8;
constexpr double kPi = 3.14159265358979323846;

uint64_t fnv1a64(const char* data, size_t len, uint64_t hash)
{
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Orthonormal DCT-II cosine table, row-major [u * N + x].
const std::vector<double>& cosTable()
{
    static const std::vector<double> table = [] {
        std::vector<double> t(kDctN * kDctN);
        for (int u = 0; u < kDctN; ++u)
            for (int x = 0; x < kDctN; ++x)
                t[u * kDctN + x] = std::cos(kPi / kDctN * (x + 0.5) * u);
        return t;
    }();
    return table;
}

double dctScale(int u)
{
    return u == 0 ? std::sqrt(1.0 / kDctN) : std::sqrt(2.0 / kDctN);
}

void dct2(const float* in, double* out)
{
    const auto& cosT = cosTable();
    std::vector<double> tmp(kDctN * kDctN);

    for (int y = 0; y < kDctN; ++y) {
        for (int u = 0; u < kDctN; ++u) {
            double s = 0.0;
            const double* row = cosT.data() + u * kDctN;
            const float* src = in + y * kDctN;
            for (int x = 0; x < kDctN; ++x)
                s += src[x] * row[x];
            tmp[y * kDctN + u] = s * dctScale(u);
        }
    }
    for (int u = 0; u < kDctN; ++u) {
        for (int v = 0; v < kDctN; ++v) {
            double s = 0.0;
            const double* col = cosT.data() + v * kDctN;
            for (int y = 0; y < kDctN; ++y)
                s += tmp[y * kDctN + u] * col[y];
            out[v * kDctN + u] = s * dctScale(v);
        }
    }
}

QImage toGrayScaled(const QImage& image, int w, int h)
{
    return image.convertToFormat(QImage::Format_Grayscale8)
        .scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace

uint64_t fileHash(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;

    uint64_t hash = 14695981039346656037ULL;
    char buf[65536];
    for (;;) {
        const qint64 n = f.read(buf, sizeof(buf));
        if (n <= 0)
            break;
        hash = fnv1a64(buf, static_cast<size_t>(n), hash);
    }
    return hash;
}

uint64_t computePHash(const QImage& image)
{
    const QImage s = toGrayScaled(image, kDctN, kDctN);

    float px[kDctN * kDctN];
    for (int y = 0; y < kDctN; ++y) {
        const uchar* line = s.constScanLine(y);
        for (int x = 0; x < kDctN; ++x)
            px[y * kDctN + x] = static_cast<float>(line[x]);
    }

    double dct[kDctN * kDctN];
    dct2(px, dct);

    double coeffs[kHashSide * kHashSide];
    for (int y = 0; y < kHashSide; ++y)
        for (int x = 0; x < kHashSide; ++x)
            coeffs[y * kHashSide + x] = dct[y * kDctN + x];

    double sorted[kHashSide * kHashSide];
    std::copy(std::begin(coeffs), std::end(coeffs), sorted);
    std::sort(sorted, sorted + kHashSide * kHashSide);
    const double median =
        (sorted[31] + sorted[32]) / 2.0;

    uint64_t bits = 0;
    for (int i = 0; i < kHashSide * kHashSide; ++i)
        if (coeffs[i] >= median)
            bits |= (1ULL << i);
    return bits;
}

uint64_t computeDHash(const QImage& image)
{
    const QImage s = toGrayScaled(image, 9, 8);

    uint64_t bits = 0;
    int i = 0;
    for (int y = 0; y < 8; ++y) {
        const uchar* line = s.constScanLine(y);
        for (int x = 0; x < 8; ++x) {
            if (line[x] > line[x + 1])
                bits |= (1ULL << i);
            ++i;
        }
    }
    return bits;
}

std::array<uint8_t, kHistBins> computeHist(const QImage& image)
{
    std::array<uint8_t, kHistBins> hist{};
    hist.fill(0);

    const QImage img = image.convertToFormat(QImage::Format_RGB888);

    // Stride-sample very large images for speed.
    const qint64 totalPixels = qint64(img.width()) * img.height();
    const int stride = totalPixels > 500000
        ? int(std::sqrt(double(totalPixels) / 500000.0)) + 1
        : 1;

    double counts[kHistBins] = {};
    double sum = 0.0;

    for (int y = 0; y < img.height(); y += stride) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < img.width(); x += stride) {
            const QColor c(line[x * 3], line[x * 3 + 1], line[x * 3 + 2]);
            const int h = c.hue(); // -1 when achromatic
            const int s = c.saturation();
            const int hBin = h < 0 ? 0 : std::min(15, h * 16 / 360);
            const int sBin = std::min(15, s * 16 / 256);
            counts[hBin * 16 + sBin] += 1.0;
            sum += 1.0;
        }
    }

    if (sum <= 0.0)
        return hist;

    for (int i = 0; i < kHistBins; ++i) {
        const double p = counts[i] / sum;
        hist[i] = static_cast<uint8_t>(std::lround(p * 255.0));
    }
    return hist;
}

bool extractFeatures(const QString& path, Features& out)
{
    out.fileHash = fileHash(path);
    if (out.fileHash == 0)
        return false;

    const QImage img(path);
    if (img.isNull())
        return false;

    out.phash = computePHash(img);
    out.dhash = computeDHash(img);
    out.hist = computeHist(img);
    return true;
}

double hammingSimilarity(uint64_t a, uint64_t b)
{
    const uint64_t x = a ^ b;
    const int dist = __builtin_popcountll(x);
    return 1.0 - double(dist) / 64.0;
}

double histIntersection(const std::array<uint8_t, kHistBins>& a,
                        const std::array<uint8_t, kHistBins>& b)
{
    int inter = 0;
    for (int i = 0; i < kHistBins; ++i)
        inter += std::min(a[i], b[i]);
    return std::min(1.0, double(inter) / 255.0);
}

double combineScore(const Features& a, const Features& b)
{
    if (isExactMatch(a, b))
        return 1.0;
    return 0.45 * hammingSimilarity(a.phash, b.phash)
        + 0.35 * hammingSimilarity(a.dhash, b.dhash)
        + 0.20 * histIntersection(a.hist, b.hist);
}

bool isExactMatch(const Features& a, const Features& b)
{
    return a.fileHash != 0 && a.fileHash == b.fileHash;
}

} // namespace core
