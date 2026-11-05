#pragma once

#include <array>
#include <cstdint>

#include <QImage>
#include <QString>

namespace core {

inline constexpr int kHistBins = 256;

struct Features {
    uint64_t fileHash = 0;
    uint64_t phash = 0;
    uint64_t dhash = 0;
    qint64 size = -1;
    qint64 mtimeMs = 0;
    std::array<uint8_t, kHistBins> hist{};
};

uint64_t fileHash(const QString& path);

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

bool isStale(const Features& f, const QString& path);

double prefilterScore(const Features& query, const Features& entry);

}
