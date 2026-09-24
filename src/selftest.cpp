#include "selftest.h"

#include "core/index.h"

#include <cstdio>
#include <vector>

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>

namespace {

QImage sceneA()
{
    QImage img(320, 240, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 320, 240);
    g.setColorAt(0, QColor(10, 40, 120));
    g.setColorAt(1, QColor(40, 180, 220));
    p.fillRect(img.rect(), g);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(220, 40, 40));
    p.drawEllipse(QPoint(120, 100), 70, 55);
    p.setBrush(QColor(240, 200, 30));
    p.drawRect(200, 150, 80, 60);
    p.end();
    return img;
}

QImage sceneB()
{
    QImage img(320, 240, QImage::Format_RGB32);
    QPainter p(&img);
    p.fillRect(img.rect(), QColor(20, 90, 30));
    p.setPen(Qt::NoPen);
    for (int x = 0; x < 320; x += 40)
        p.fillRect(x, 0, 20, 240, QColor(230, 240, 220));
    p.setBrush(QColor(250, 250, 250));
    p.drawEllipse(QPoint(160, 120), 50, 50);
    p.end();
    return img;
}

QImage sceneC()
{
    QImage img(320, 240, QImage::Format_RGB32);
    QPainter p(&img);
    p.fillRect(img.rect(), QColor(70, 25, 90));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(240, 140, 40));
    p.drawEllipse(QPoint(170, 110), 90, 70);
    p.setBrush(QColor(30, 180, 170));
    p.drawRect(10, 170, 90, 55);
    p.end();
    return img;
}

QImage hueShift(const QImage& in, int delta)
{
    QImage img = in.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < img.height(); ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            QColor c(line[x * 3], line[x * 3 + 1], line[x * 3 + 2]);
            if (c.hue() >= 0) {
                const QColor s = QColor::fromHsv(
                    (c.hue() + delta) % 360, c.saturation(), c.value());
                line[x * 3] = uchar(s.red());
                line[x * 3 + 1] = uchar(s.green());
                line[x * 3 + 2] = uchar(s.blue());
            }
        }
    }
    return img;
}

int rankOf(const std::vector<core::SearchResult>& results,
           const QString& fileName)
{
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].relPath.endsWith(fileName) || results[i].relPath == fileName)
            return int(i);
    }
    return -1;
}

void printResults(const std::vector<core::SearchResult>& results)
{
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::printf("  %2zu  %6.3f  %s  %s\n", i + 1, r.score,
                    r.exact ? "EXACT" : "     ",
                    qPrintable(r.relPath));
    }
}

} // namespace

int runSelfTest()
{
    int failures = 0;
    const QString base =
        QDir::tempPath() + QStringLiteral("/image_search_selftest");
    QDir(base).removeRecursively();
    const QString lib = base + QStringLiteral("/library");
    const QString qdir = base + QStringLiteral("/query");
    QDir().mkpath(lib);
    QDir().mkpath(qdir);

    const QImage a = sceneA();
    const QString original = lib + QStringLiteral("/original.png");
    a.save(original, "PNG");

    // Byte-identical copies.
    QFile::copy(original, lib + QStringLiteral("/copy.png"));

    // Variants.
    a.scaled(a.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .save(lib + QStringLiteral("/resized.png"), "PNG");
    a.save(lib + QStringLiteral("/recompressed.jpg"), "JPG", 70);
    hueShift(a, 50).save(lib + QStringLiteral("/hueshift.png"), "PNG");

    // Distractors.
    sceneB().save(lib + QStringLiteral("/sceneB.png"), "PNG");
    sceneC().save(lib + QStringLiteral("/sceneC.png"), "PNG");

    core::ImageIndex index;
    if (!index.build(lib)) {
        std::printf("FAIL: could not build index\n");
        return 1;
    }
    std::printf("Indexed %zu images (%d skipped)\n\n",
                size_t(index.size()), index.errorCount());

    // --- Case 1: byte-identical query ---
    const QString qExact = qdir + QStringLiteral("/exact_query.png");
    QFile::copy(original, qExact);

    std::vector<core::SearchResult> r1;
    if (!index.searchFile(qExact, 20, r1)) {
        std::printf("FAIL: exact query unreadable\n");
        ++failures;
    } else {
        std::printf("Case 1 — byte-identical query:\n");
        printResults(r1);

        const int exactCount = [&] {
            int n = 0;
            for (const auto& r : r1)
                if (r.exact)
                    ++n;
            return n;
        }();

        if (r1.size() < 2 || !r1[0].exact || !r1[1].exact) {
            std::printf("FAIL: expected 2 EXACT results at ranks 1-2\n\n");
            ++failures;
        } else if (exactCount != 2) {
            std::printf("FAIL: expected exactly 2 EXACT results, got %d\n\n",
                        exactCount);
            ++failures;
        } else {
            std::printf("PASS: original.png and copy.png both EXACT at top\n\n");
        }
    }

    // --- Case 2: visually similar query (60%% rescale, not in library) ---
    const QString qSim = qdir + QStringLiteral("/similar_query.png");
    a.scaled(a.size() * 6 / 10, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .save(qSim, "PNG");

    std::vector<core::SearchResult> r2;
    if (!index.searchFile(qSim, 20, r2)) {
        std::printf("FAIL: similar query unreadable\n");
        ++failures;
    } else {
        std::printf("Case 2 — 60%% rescale query:\n");
        printResults(r2);

        const int rankOrig = rankOf(r2, QStringLiteral("original.png"));
        const int rankCopy = rankOf(r2, QStringLiteral("copy.png"));
        const int rankResized = rankOf(r2, QStringLiteral("resized.png"));
        const int rankRejpg = rankOf(r2, QStringLiteral("recompressed.jpg"));
        const int rankHue = rankOf(r2, QStringLiteral("hueshift.png"));
        const int rankB = rankOf(r2, QStringLiteral("sceneB.png"));
        const int rankC = rankOf(r2, QStringLiteral("sceneC.png"));

        const int bestGood = std::min(
            std::min(rankOrig, rankCopy),
            std::min(rankResized, rankRejpg));

        if (bestGood < 0 || rankB < 0 || rankC < 0) {
            std::printf("FAIL: missing expected results\n\n");
            ++failures;
        } else if (bestGood != 0) {
            std::printf("FAIL: expected a true variant at rank 1, got rank %d\n\n",
                        bestGood + 1);
            ++failures;
        } else if (bestGood >= rankB || bestGood >= rankC) {
            std::printf("FAIL: distractors outranked true variants\n\n");
            ++failures;
        } else if (rankHue < 0 || rankHue > rankB || rankHue > rankC) {
            std::printf("FAIL: hueshift should rank above distractors "
                        "(hue=%d B=%d C=%d)\n\n",
                        rankHue, rankB, rankC);
            ++failures;
        } else {
            std::printf("PASS: variants on top, hueshift mid, "
                        "distractors last\n\n");
        }
        for (const auto& r : r2) {
            if (r.exact) {
                std::printf("FAIL: no EXACT expected for rescaled query\n\n");
                ++failures;
                break;
            }
        }
    }

    // --- Case 3: index round-trip ---
    const QString idxPath = base + QStringLiteral("/index.bin");
    core::ImageIndex reloaded;
    if (!index.save(idxPath) || !reloaded.load(idxPath)
        || reloaded.size() != index.size()) {
        std::printf("FAIL: index save/load round-trip\n");
        ++failures;
    } else {
        std::printf("PASS: index save/load round-trip (%zu entries)\n",
                    size_t(reloaded.size()));
    }

    if (failures == 0)
        std::printf("\nAll self-tests passed.\n");
    else
        std::printf("\n%d self-test failure(s).\n", failures);
    return failures;
}
