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
    std::array<uint8_t, kHistBins> hist{};
};

uint64_t fileHash(const QString& path);

uint64_t computePHash(const QImage& image);
uint64_t computeDHash(const QImage& image);
std::array<uint8_t, kHistBins> computeHist(const QImage& image);

bool extractFeatures(const QString& path, Features& out);

double hammingSimilarity(uint64_t a, uint64_t b);
double histIntersection(const std::array<uint8_t, kHistBins>& a,
                        const std::array<uint8_t, kHistBins>& b);
double combineScore(const Features& a, const Features& b);
bool isExactMatch(const Features& a, const Features& b);

} // namespace core
