#include "selftest.h"

#include "app/update_checker.h"
#include "core/background_remover.h"
#include "core/features.h"
#include "core/index.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QSettings>

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

QImage texturedScene()
{
    QImage img(320, 240, QImage::Format_RGB32);
    QPainter p(&img);
    p.fillRect(img.rect(), QColor(60, 70, 90));

    quint32 seed = 0x9E3779B9u;
    const auto next = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    for (int by = 0; by < 240; by += 4)
        for (int bx = 0; bx < 320; bx += 4) {
            const int v = int(next() % 200) + 20;
            p.fillRect(bx, by, 4, 4, QColor(v, (v * 3) % 256, (v * 7) % 256));
        }

    p.setPen(QPen(QColor(250, 250, 250), 3));
    p.drawLine(0, 200, 320, 40);
    p.drawLine(0, 60, 320, 190);
    p.setPen(QPen(QColor(20, 20, 20), 5));
    p.drawEllipse(QRectF(90, 70, 140, 110));
    p.end();
    return img;
}

QImage exposureShift(const QImage& in, double gain, double bias)
{
    QImage img = in.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < img.height(); ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x)
            for (int c = 0; c < 3; ++c) {
                const int v = int(qRound(double(line[x * 3 + c]) * gain + bias));
                line[x * 3 + c] = uchar(qBound(0, v, 255));
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

}

int runSelfTest()
{
    int failures = 0;

    const QString base = QDir::tempPath() +
                         QStringLiteral("/lucidgrasp_selftest_") +
                         QString::number(QCoreApplication::applicationPid());
    QDir(base).removeRecursively();
    QDir(base).mkpath(QStringLiteral("."));
    const QString lib = base + QStringLiteral("/library");
    const QString qdir = base + QStringLiteral("/query");
    QDir().mkpath(lib);
    QDir().mkpath(qdir);

    const QImage a = sceneA();
    const QString original = lib + QStringLiteral("/original.png");
    a.save(original, "PNG");

    QFile::copy(original, lib + QStringLiteral("/copy.png"));

    a.scaled(a.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .save(lib + QStringLiteral("/resized.png"), "PNG");
    a.save(lib + QStringLiteral("/recompressed.jpg"), "JPG", 70);
    hueShift(a, 50).save(lib + QStringLiteral("/hueshift.png"), "PNG");

    sceneB().save(lib + QStringLiteral("/sceneB.png"), "PNG");
    sceneC().save(lib + QStringLiteral("/sceneC.png"), "PNG");

    core::ImageIndex index;
    if (!index.build(lib)) {
        std::printf("FAIL: could not build index\n");
        return 1;
    }
    std::printf("Indexed %zu images (%d skipped)\n\n",
                size_t(index.size()), index.errorCount());

    const QString qExact = qdir + QStringLiteral("/exact_query.png");
    QFile::copy(original, qExact);

    std::vector<core::SearchResult> r1;
    if (!index.searchFile(qExact, 0.0, r1)) {
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
        } else if (r1[0].score < 0.9 || r1[1].score < 0.9) {
            std::printf("FAIL: exact copies scored %.3f/%.3f; a keypoint-free "
                        "pair should reach ~1.0, not the ORB-silent ceiling\n\n",
                        r1[0].score, r1[1].score);
            ++failures;
        } else {
            std::printf("PASS: original.png and copy.png both EXACT at top, "
                        "%.0f%% on a keypoint-free scene\n\n",
                        r1[0].score * 100.0);
        }
    }

    const QString qSim = qdir + QStringLiteral("/similar_query.png");
    a.scaled(a.size() * 6 / 10, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .save(qSim, "PNG");

    std::vector<core::SearchResult> r2;
    if (!index.searchFile(qSim, 0.0, r2)) {
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

    const QString tlib = base + QStringLiteral("/textured_library");
    QDir().mkpath(tlib);

    const QImage tex = texturedScene();
    tex.save(tlib + QStringLiteral("/original.png"), "PNG");
    QFile::copy(tlib + QStringLiteral("/original.png"),
                tlib + QStringLiteral("/copy.png"));
    exposureShift(tex, 0.75, 40.0)
        .save(tlib + QStringLiteral("/graded.png"), "PNG");
    sceneA().save(tlib + QStringLiteral("/distractA.png"), "PNG");
    sceneB().save(tlib + QStringLiteral("/distractB.png"), "PNG");
    sceneC().save(tlib + QStringLiteral("/distractC.png"), "PNG");

    core::ImageIndex tindex;
    QString qTex;
    if (!tindex.build(tlib)) {
        std::printf("FAIL: could not build textured index\n");
        ++failures;
    } else {
        qTex = qdir + QStringLiteral("/textured_query.png");
        QFile::copy(tlib + QStringLiteral("/original.png"), qTex);

        std::vector<core::SearchResult> r4;
        if (!tindex.searchFile(qTex, 0.0, r4)) {
            std::printf("FAIL: textured query unreadable\n\n");
            ++failures;
        } else {
            std::printf("Case 4 — textured corpus (ORB live):\n");
            printResults(r4);

            if (r4.size() < 2 || r4[0].score < 0.99 || r4[1].score < 0.99) {
                std::printf("FAIL: exact copies scored %.3f/%.3f, expected ~1.0 "
                            "(ORB likely not contributing)\n\n",
                            r4.empty() ? 0.0 : r4[0].score,
                            r4.size() < 2 ? 0.0 : r4[1].score);
                ++failures;
            } else {
                std::printf("PASS: exact copies at %.3f, ORB path is live\n",
                            r4[0].score);
            }

            const int rankGraded = rankOf(r4, QStringLiteral("graded.png"));
            const int rankA = rankOf(r4, QStringLiteral("distractA.png"));
            const int rankB = rankOf(r4, QStringLiteral("distractB.png"));
            const int rankC = rankOf(r4, QStringLiteral("distractC.png"));
            if (rankGraded < 0 || rankA < 0 || rankB < 0 || rankC < 0) {
                std::printf("FAIL: missing expected textured results\n\n");
                ++failures;
            } else if (rankGraded > std::min({rankA, rankB, rankC})) {
                std::printf("FAIL: graded edit ranked below a distractor "
                            "(graded=%d A=%d B=%d C=%d)\n\n",
                            rankGraded, rankA, rankB, rankC);
                ++failures;
            } else {
                std::printf("PASS: graded edit outranks all distractors\n\n");
            }
        }
    }

    {
        const uint64_t hp0 = core::computePHash(tex);
        const uint64_t hpBright = core::computePHash(exposureShift(tex, 1.35, -30.0));
        const uint64_t hpDark = core::computePHash(exposureShift(tex, 0.70, 25.0));
        const double simBright = core::hammingSimilarity(hp0, hpBright);
        const double simDark = core::hammingSimilarity(hp0, hpDark);

        std::printf("Case 5 — pHash under exposure change:\n"
                    "  brighter similarity %.3f, darker similarity %.3f\n",
                    simBright, simDark);

        if (simBright < 0.90 || simDark < 0.90) {
            std::printf("FAIL: pHash unstable under exposure change; a graded "
                        "photo would fall out of the shortlist\n\n");
            ++failures;
        } else {
            std::printf("PASS: pHash holds across exposure changes\n\n");
        }
    }

    {
        struct VersionCase {
            const char *candidate;
            const char *current;
            bool newer;
        };
        const VersionCase cases[] = {
            {"v1.2.0", "1.1.0", true},
            {"1.2.0", "1.1.0", true},
            {"v1.1.1", "1.1.0", true},
            {"v2.0.0", "1.99.99", true},
            {"v1.2", "1.1.9", true},
            {"v2", "1.99.99", true},
            {"v1.1.0", "1.1.0", false},
            {"v1.1.0", "v1.1.0", false},
            {"v1.0.9", "1.1.0", false},
            {"v0.9.9", "1.0.0", false},
            {"v1.2.0", "1.2.0-rc1", true},
            {"v1.2.0-rc1", "1.2.0", false},
            {"v1.2.0+build7", "1.2.0", false},
            {"v1.2.0-rc1", "1.2.0-rc2", false},
            {"", "1.1.0", false},
            {"v1.1.0", "", false},
            {"nightly", "1.1.0", false},
            {"v1.2.x", "1.1.0", false},
            {"v1.2.3.4", "1.1.0", false},
            {"v1.2.", "1.1.0", false},
            {"v2024.01.15", "1.1.0", false},
            {"v99999999999999999999", "1.1.0", false},
        };

        std::printf("Case 6 — update version comparison:\n");
        int caseFailures = 0;
        for (const auto &c : cases) {
            const bool got = app::isNewerVersion(QString::fromLatin1(c.candidate),
                                                QString::fromLatin1(c.current));
            if (got != c.newer) {
                std::printf("  FAIL: \"%s\" vs \"%s\" -> %s, expected %s\n",
                            c.candidate, c.current, got ? "newer" : "not newer",
                            c.newer ? "newer" : "not newer");
                ++caseFailures;
            }
        }
        if (caseFailures) {
            std::printf("FAIL: %d version comparison(s) wrong\n\n", caseFailures);
            ++failures;
        } else {
            std::printf("PASS: %zu comparisons correct, all malformed tags silent\n\n",
                        sizeof(cases) / sizeof(cases[0]));
        }

        if (app::normaliseVersion(QStringLiteral("v1.2.0")) !=
                QStringLiteral("1.2.0") ||
            app::normaliseVersion(QStringLiteral("  V1.2.0 ")) !=
                QStringLiteral("1.2.0")) {
            std::printf("FAIL: tag normalisation is wrong\n\n");
            ++failures;
        }
    }

    {
        const QByteArray good =
            R"({"tag_name":"v1.2.0","name":"1.2.0",)"
            R"("html_url":"https://github.com/dialga-cmd/LucidGrasp/releases/tag/v1.2.0",)"
            R"("body":"  Faster indexing.  ","draft":false,"prerelease":false})";

        app::ReleaseInfo info;
        if (!app::parseLatestRelease(good, &info)) {
            std::printf("Case 7 — release payload parsing:\n");
            std::printf("FAIL: a valid payload was rejected\n\n");
            ++failures;
        } else {
            const bool ok = info.tag == QStringLiteral("v1.2.0") &&
                            info.url.startsWith(QLatin1String("https://")) &&
                            info.notes == QStringLiteral("Faster indexing.");
            if (!ok) {
                std::printf("Case 7 — release payload parsing:\n");
                std::printf("FAIL: payload fields wrong (tag \"%s\", notes \"%s\")\n\n",
                            qPrintable(info.tag), qPrintable(info.notes));
                ++failures;
            } else {
                std::printf("Case 7 — release payload parsing:\n");
                std::printf("PASS: valid payload read\n");
            }
        }

        const QByteArray bad[] = {
            QByteArrayLiteral(""),
            QByteArrayLiteral("{"),
            QByteArrayLiteral("[]"),
            QByteArrayLiteral("not json at all"),
            QByteArrayLiteral("<html><body>503</body></html>"),
            R"({"html_url":"https://example.com","body":"x"})",
            R"({"tag_name":"","html_url":"https://example.com"})",
            R"({"tag_name":"v1.2.0"})",
            R"({"tag_name":"v1.2.0","html_url":"https://x/","draft":true})",
            R"({"tag_name":"v1.2.0","html_url":"https://x/","prerelease":true})",
        };

        int rejected = 0;
        for (const QByteArray &payload : bad) {
            app::ReleaseInfo untouched;
            untouched.tag = QStringLiteral("sentinel");
            if (!app::parseLatestRelease(payload, &untouched) &&
                untouched.tag == QStringLiteral("sentinel"))
                ++rejected;
        }
        if (rejected != int(sizeof(bad) / sizeof(bad[0]))) {
            std::printf("FAIL: %d of %zu malformed payloads were not cleanly refused\n\n",
                        int(sizeof(bad) / sizeof(bad[0])) - rejected,
                        sizeof(bad) / sizeof(bad[0]));
            ++failures;
        } else {
            std::printf("PASS: %zu malformed payloads refused, out untouched\n\n",
                        sizeof(bad) / sizeof(bad[0]));
        }
    }

    {
        const QString scratch = base + QStringLiteral("/settings");
        QDir().mkpath(scratch);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch);
        QSettings::setDefaultFormat(QSettings::IniFormat);

        std::printf("Case 8 — update preferences:\n");
        int caseFailures = 0;
        auto require = [&caseFailures](bool cond, const char *what) {
            if (!cond) {
                std::printf("  FAIL: %s\n", what);
                ++caseFailures;
            }
        };

        app::UpdateSettings s;
        require(!s.isDisabled(), "checks should be on by default");

        s.setDisabled(true);
        require(s.isDisabled(), "opt-out did not persist");
        s.setDisabled(false);
        require(!s.isDisabled(), "re-enabling did not persist");

        s.setIgnoredVersion(QStringLiteral("v1.2.0"));
        require(s.ignoredVersion() == QStringLiteral("1.2.0"),
                "the muted version was not normalised");
        s.setIgnoredVersion(QStringLiteral("1.3.0"));
        require(s.ignoredVersion() == QStringLiteral("1.3.0"),
                "muting a second version overwrote nothing it should have");

        if (caseFailures) {
            std::printf("FAIL: %d preference check(s) wrong\n\n", caseFailures);
            ++failures;
        } else {
            std::printf("PASS: opt-out and mute hold\n\n");
        }
    }

    if (!tindex.empty() && !qTex.isEmpty()) {
        std::vector<core::SearchResult> r9;
        if (!tindex.searchFile(qTex, 0.5, r9)) {
            std::printf("FAIL: default-threshold query unreadable\n\n");
            ++failures;
        } else {
            std::printf("Case 9 — default threshold (50%%):\n");
            printResults(r9);

            const int rankOrig = rankOf(r9, QStringLiteral("original.png"));
            const int rankGraded = rankOf(r9, QStringLiteral("graded.png"));
            const int dA = rankOf(r9, QStringLiteral("distractA.png"));
            const int dB = rankOf(r9, QStringLiteral("distractB.png"));
            const int dC = rankOf(r9, QStringLiteral("distractC.png"));
            const bool distractorAbove =
                (dA >= 0 && rankGraded > dA) || (dB >= 0 && rankGraded > dB) ||
                (dC >= 0 && rankGraded > dC);

            if (rankOrig < 0 || rankGraded < 0) {
                std::printf("FAIL: original/graded edit did not clear the "
                            "default 50%% threshold\n\n");
                ++failures;
            } else if (distractorAbove) {
                std::printf("FAIL: graded edit clears the default threshold "
                            "but ranks below a distractor\n\n");
                ++failures;
            } else {
                std::printf("PASS: original and graded edit clear the default "
                            "50%% threshold\n\n");
            }
        }
    }

    std::printf("Case 10 — index entries removed on delete:\n");
    {
        const QString libdir = base + QStringLiteral("/deletelib");
        QDir().mkpath(libdir);

        for (const char *name : {"alpha.png", "beta.png", "gamma.png"}) {
            QImage img(24, 24, QImage::Format_RGB32);
            img.fill(Qt::white);
            img.save(QString(libdir) + '/' + name, "PNG");
        }

        core::ImageIndex victim;
        std::printf("PASS: built a 3-image library\n");
        if (!victim.build(libdir) || victim.size() != 3) {
            std::printf("FAIL: could not build the test library (%zu entries)\n\n",
                        size_t(victim.size()));
            ++failures;
        } else {
            const bool removed = victim.removeEntry(QStringLiteral("beta.png"));
            const size_t after = victim.size();
            if (!removed) {
                std::printf("FAIL: removeEntry did not find an indexed file\n\n");
                ++failures;
            } else if (after != 2) {
                std::printf("FAIL: expected 2 entries left, got %zu\n\n",
                            size_t(after));
                ++failures;
            } else {
                std::printf("PASS: one entry removed, two left\n");
            }

            if (victim.removeEntry(QStringLiteral("beta.png"))) {
                std::printf("FAIL: removing the same entry twice claimed "
                            "success\n\n");
                ++failures;
            } else {
                std::printf("PASS: second removal of the same entry refused\n");
            }

            if (victim.removeEntry(libdir + QStringLiteral("/gamma.png"))) {
                std::printf("FAIL: an absolute path matched a relative key\n\n");
                ++failures;
            } else if (victim.size() != 2) {
                std::printf("FAIL: a failed removal still changed the size\n\n");
                ++failures;
            } else {
                std::printf("PASS: absolute path and unknown key both refused\n");
            }

            const QString survivor = libdir + QStringLiteral("/alpha.png");
            std::vector<core::SearchResult> found;
            if (!victim.searchFile(survivor, 0.0, found) || found.empty() ||
                found.front().relPath != QStringLiteral("alpha.png")) {
                std::printf("FAIL: survivors are not searchable after a "
                            "removal\n\n");
                ++failures;
            } else {
                std::printf("PASS: surviving entries still searchable\n\n");
            }
        }
    }

    std::printf("Case 11 — damaged caches refused, saves are atomic:\n");
    {
        const QString good = base + QStringLiteral("/index.bin");
        QFile src(good);
        if (!src.open(QIODevice::ReadOnly)) {
            std::printf("FAIL: could not read the round-tripped index\n\n");
            ++failures;
        } else {
            const QByteArray bytes = src.readAll();
            src.close();

            const QString cut = base + QStringLiteral("/truncated.bin");
            QFile out(cut);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(bytes.left(bytes.size() / 2));
                out.close();
            }
            core::ImageIndex damaged;
            if (damaged.load(cut) || !damaged.empty()) {
                std::printf("FAIL: a truncated cache was accepted (%zu entries)\n",
                            size_t(damaged.size()));
                ++failures;
            } else {
                std::printf("PASS: truncated cache refused, index left empty\n");
            }

            const QString junk = base + QStringLiteral("/junk.bin");
            QFile out2(junk);
            if (out2.open(QIODevice::WriteOnly)) {
                out2.write(QByteArray(4096, '\x5a'));
                out2.close();
            }
            core::ImageIndex junkIndex;
            if (junkIndex.load(junk) || !junkIndex.empty()) {
                std::printf("FAIL: a cache with no valid header was accepted\n");
                ++failures;
            } else {
                std::printf("PASS: headerless cache refused\n");
            }
        }

        const QString adir = base + QStringLiteral("/atomic");
        QDir().mkpath(adir);
        const QString apath = adir + QStringLiteral("/cache.bin");

        core::ImageIndex first;
        if (!first.build(lib) || first.empty()) {
            std::printf("FAIL: could not build an index for the atomicity case\n\n");
            ++failures;
        } else {
            if (!first.save(apath)) {
                std::printf("FAIL: first atomic save reported failure\n\n");
                ++failures;
            } else {
                core::ImageIndex second;
                if (!second.build(tlib)) {
                    std::printf("FAIL: could not build the second index\n\n");
                    ++failures;
                } else if (!second.save(apath)) {
                    std::printf("FAIL: second atomic save reported failure\n\n");
                    ++failures;
                } else {
                    core::ImageIndex reloaded;
                    const bool replaced =
                        reloaded.load(apath)
                        && reloaded.size() == second.size()
                        && reloaded.root() == second.root();
                    if (!replaced) {
                        std::printf("FAIL: the second save did not cleanly "
                                    "replace the first\n\n");
                        ++failures;
                    } else {
                        std::printf("PASS: second save cleanly replaced the "
                                    "first (%zu entries)\n",
                                    size_t(reloaded.size()));
                    }
                }
            }

            const QStringList left =
                QDir(adir).entryList(QDir::Files | QDir::Hidden | QDir::System);
            if (left.size() != 1) {
                std::printf("FAIL: save left %d files behind (%s); expected "
                            "only the cache\n\n",
                            int(left.size()), qPrintable(left.join(", ")));
                ++failures;
            } else {
                std::printf("PASS: no temp file left behind\n");
            }

            core::ImageIndex keep;
            const bool keptBefore = keep.load(apath) && !keep.empty();
            const bool badSave =
                keep.save(base + QStringLiteral("/no_such_dir/cache.bin"));
            core::ImageIndex after;
            const bool keptAfter = after.load(apath) && !after.empty();
            if (badSave) {
                std::printf("FAIL: a save into a missing directory reported "
                            "success\n\n");
                ++failures;
            } else if (keptBefore != keptAfter || !keptAfter) {
                std::printf("FAIL: a failed save damaged the existing cache\n\n");
                ++failures;
            } else {
                std::printf("PASS: failed save left the existing cache intact\n\n");
            }
        }
    }

    std::printf("Case 12 — background-removal preprocessing:\n");
    {
        QImage red(64, 48, QImage::Format_RGB32);
        red.fill(QColor(255, 0, 0));
        std::vector<float> tensor;
        if (!core::BackgroundRemover::loadInput(red, 32, &tensor) ||
            tensor.size() != size_t(3) * 32 * 32) {
            std::printf("FAIL: tensor sizing for a solid red image\n\n");
            ++failures;
        } else {
            const float mean[3] = {0.485f, 0.456f, 0.406f};
            const float stdv[3] = {0.229f, 0.224f, 0.225f};
            const float expect[3] = {
                (1.0f - mean[0]) / stdv[0],
                (0.0f - mean[1]) / stdv[1],
                (0.0f - mean[2]) / stdv[2],
            };
            bool match = true;
            for (int c = 0; c < 3 && match; ++c)
                for (size_t i = 0; i < size_t(32) * 32; ++i) {
                    const float got = tensor[c * size_t(32) * 32 + i];
                    if (std::fabs(got - expect[c]) > 1e-3f) {
                        match = false;
                        break;
                    }
                }
            if (!match) {
                std::printf("FAIL: normalized pixel values wrong\n\n");
                ++failures;
            } else {
                std::printf("PASS: RGB->NCHW normalization matches the "
                            "BiRefNet convention\n\n");
            }
        }

        QImage black(32, 32, QImage::Format_RGB32);
        black.fill(Qt::black);
        std::vector<float> t2;
        if (core::BackgroundRemover::loadInput(black, 32, &t2)) {
            std::printf("FAIL: a fully black image produced a tensor\n\n");
            ++failures;
        } else {
            std::printf("PASS: a fully black image is rejected\n\n");
        }

        core::BackgroundRemover remover;
        QImage cutout;
        QString noModelError;
        const bool ranNoModel =
            remover.removeBackground(red, &cutout, &noModelError);
        if (ranNoModel) {
            std::printf("FAIL: inference without a model reported success\n\n");
            ++failures;
        } else {
            std::printf("PASS: inference without a model fails cleanly\n\n");
        }
    }

    std::printf("Case 13 — cutout matte compositing:\n");
    {
        QImage cutout(2, 2, QImage::Format_ARGB32);
        cutout.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        cutout.setPixelColor(1, 0, QColor(0, 255, 0, 128));
        cutout.setPixelColor(0, 1, QColor(0, 0, 255, 0));
        cutout.setPixelColor(1, 1, QColor(255, 255, 255, 255));

        const QImage matte = core::matteOf(cutout);
        int bad = 0;
        if (matte.isNull() || matte.width() != 2 || matte.height() != 2 ||
            matte.format() != QImage::Format_RGB888) {
            std::printf("FAIL: matte size or format\n\n");
            ++failures;
        } else {
            if (matte.pixelColor(0, 0) != QColor(255, 0, 0)) {
                std::printf("FAIL: opaque pixel lost its color\n");
                ++bad;
            }
            const QColor gray = matte.pixelColor(0, 1);
            if (gray != QColor(128, 128, 128)) {
                std::printf("FAIL: transparent pixel did not become gray "
                            "(got %d,%d,%d)\n",
                            gray.red(), gray.green(), gray.blue());
                ++bad;
            }
            const QColor blend = matte.pixelColor(1, 0);
            const int dr = std::abs(blend.red() - 64);
            const int dg = std::abs(blend.green() - 191);
            const int db = std::abs(blend.blue() - 64);
            if (dr > 1 || dg > 1 || db > 1) {
                std::printf("FAIL: half-transparent pixel not blended "
                            "(got %d,%d,%d)\n",
                            blend.red(), blend.green(), blend.blue());
                ++bad;
            }
            if (matte.pixelColor(1, 1) != QColor(255, 255, 255)) {
                std::printf("FAIL: opaque white changed\n");
                ++bad;
            }
            if (bad == 0) {
                std::printf("PASS: matte keeps the foreground and flattens "
                            "the background to gray\n\n");
            } else {
                std::printf("\n");
                ++failures;
            }
        }
    }

    if (failures == 0)
        std::printf("\nAll self-tests passed.\n");
    else
        std::printf("\n%d self-test failure(s).\n", failures);
    return failures;
}
