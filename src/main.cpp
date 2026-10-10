#include <cstdio>
#include <cstring>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QTextStream>

#include "core/index.h"
#include "core/semantic_embedder.h"
#include "selftest.h"
#include "ui/mainwindow.h"

namespace {

void printUsage()
{
    std::printf(
        "LucidGrasp\n"
        "\n"
        "Usage:\n"
        "  LucidGrasp                            launch GUI\n"
        "  LucidGrasp --cli <library> <query> [threshold%%]\n"
        "      threshold is 0-100, default 50\n"
        "      headless visual search; reuses cache if present\n"
        "  LucidGrasp --cli --reindex <library> <query> [threshold%%]\n"
        "      force a fresh index\n"
        "  LucidGrasp --selftest\n"
        "      build a synthetic corpus and verify ranking\n"
        "  LucidGrasp --similar [--reindex] <library> <query> [threshold%%]\n"
        "      similarity search powered by the DINOv2 embedding model;\n"
        "      ranks the library by how related the image content is rather\n"
        "      than by raw pixel similarity; the model defaults to the\n"
        "      standard model location (see ./fetch-model.sh semantic)\n"
        "  LucidGrasp --reset-legal-agreement\n"
        "      clear the stored first-run agreement; the Welcome\n"
        "      dialog is shown again and must be agreed to enter\n");
}

bool parseThreshold(const QString& text, double* threshold)
{
    bool parsed = false;
    const double pct = text.toDouble(&parsed);
    if (!parsed || pct < 0.0 || pct > 100.0)
        return false;
    *threshold = pct / 100.0;
    return true;
}

int runVisualSearch(const QString& library, const QString& query,
                    double threshold, bool reindex)
{
    core::ImageIndex index;
    QElapsedTimer timer;
    timer.start();

    bool loaded = false;
    const QString cache = core::defaultIndexPath(library);
    const QString legacy = core::legacyIndexPath(library);
    const QString loadFrom = QFileInfo::exists(cache) ? cache : legacy;

    if (!reindex && QFileInfo::exists(loadFrom) && index.load(loadFrom)
        && !index.empty()) {
        loaded = true;
    } else {
        int lastPct = -1;
        const bool built = index.build(library, [&](const core::BuildProgress& p) {
            const int pct = p.total > 0
                                ? int(qint64(p.done) * 100 / p.total)
                                : 0;
            if (pct != lastPct) {
                lastPct = pct;
                std::fprintf(stderr, "\rindexing %3d%% (%d files)   ", pct,
                             p.total);
                std::fflush(stderr);
            }
            return true;
        });
        std::fprintf(stderr, "\r");
        if (!built) {
            std::fprintf(stderr, "error: cannot index '%s'\n",
                         qPrintable(library));
            return 1;
        }
        if (!index.save(cache))
            std::fprintf(stderr, "warning: could not write index cache '%s'\n",
                         qPrintable(cache));
    }

    const qint64 indexMs = timer.elapsed();

    timer.restart();
    std::vector<core::SearchResult> results;
    int lastDone = -1;
    const auto report = [&](int done, int total) {
        if (total > 0 && done != lastDone
            && (done % 16 == 0 || done == total)) {
            lastDone = done;
            std::fprintf(stderr, "\rcomparing %d/%d   ", done, total);
            std::fflush(stderr);
        }
        return true;
    };
    bool searched = index.searchFile(query, threshold, results, report);
    if (!searched) {
        std::fprintf(stderr, "error: cannot read query '%s'\n",
                     qPrintable(query));
        return 1;
    }
    std::fprintf(stderr, "\r");
    const qint64 searchMs = timer.elapsed();

    std::printf("library : %s\n", qPrintable(library));
    std::printf("indexed : %zu images%s, %d skipped  (%lld ms, %s)\n",
                size_t(index.size()),
                loaded ? " (cached)" : "",
                index.errorCount(), static_cast<long long>(indexMs),
                loaded ? "load" : "build");
    std::printf("query   : %s\n", qPrintable(query));
    std::printf("search  : %lld ms, %zu results above %.0f%% threshold\n\n",
                static_cast<long long>(searchMs), results.size(),
                threshold * 100.0);
    std::printf(" rank   score  flag     path\n");

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::printf("%5zu  %6.3f  %-6s  %s\n", i + 1, r.score,
                    r.exact ? "EXACT" : "-", qPrintable(r.relPath));
    }
    return 0;
}

int runCli(const QStringList& args)
{
    bool reindex = false;
    QStringList rest;
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--reindex"))
            reindex = true;
        else
            rest.append(args[i]);
    }

    if (rest.size() < 2 || rest.size() > 3) {
        printUsage();
        return 2;
    }

    double threshold = 0.5;
    if (rest.size() == 3 && !parseThreshold(rest[2], &threshold)) {
        std::fprintf(stderr,
                     "error: threshold must be a number from 0 to 100, "
                     "got '%s'\n",
                     qPrintable(rest[2]));
        return 2;
    }

    return runVisualSearch(rest[0], rest[1], threshold, reindex);
}

int runSimilarSearch(const QStringList& args)
{
    bool reindex = false;
    QStringList rest;
    bool seen = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--similar"))
            seen = true;
        else if (seen && args[i] == QLatin1String("--reindex"))
            reindex = true;
        else if (seen)
            rest.append(args[i]);
    }

    if (rest.size() < 2 || rest.size() > 3) {
        printUsage();
        return 2;
    }

    const QString library = rest[0];
    const QString query = rest[1];
    double threshold = 0.5;
    if (rest.size() == 3 && !parseThreshold(rest[2], &threshold)) {
        std::fprintf(stderr,
                     "error: threshold must be a number from 0 to 100, "
                     "got '%s'\n",
                     qPrintable(rest[2]));
        return 2;
    }

    const QString model = core::defaultSemanticModelPath();
    if (model.isEmpty()) {
        std::fprintf(stderr,
                     "error: no similarity model found; run ./fetch-model.sh "
                     "semantic or set LUCIDGRASP_SEMANTIC_MODEL\n");
        return 1;
    }

    core::SemanticEmbedder embedder;
    QString error;
    QElapsedTimer timer;
    timer.start();
    if (!embedder.loadModel(model, &error)) {
        std::fprintf(stderr, "error: cannot load the similarity model: %s\n",
                     qPrintable(error));
        return 1;
    }
    const qint64 loadMs = timer.elapsed();

    core::ImageIndex index;
    const QString cache = core::defaultIndexPath(library);
    const QString legacy = core::legacyIndexPath(library);
    const QString loadFrom = QFileInfo::exists(cache) ? cache : legacy;

    bool loaded = false;
    if (!reindex && QFileInfo::exists(loadFrom) && index.load(loadFrom)
        && index.hasSemanticEmbeddings()) {
        loaded = true;
    } else {
        int lastPct = -1;
        const bool built = index.build(
            library,
            [&](const core::BuildProgress& p) {
                const int pct = p.total > 0
                                    ? int(qint64(p.done) * 100 / p.total)
                                    : 0;
                if (pct != lastPct) {
                    lastPct = pct;
                    std::fprintf(stderr, "\rindexing %3d%% (%d files)   ", pct,
                                 p.total);
                    std::fflush(stderr);
                }
                return true;
            },
            &embedder);
        std::fprintf(stderr, "\r");
        if (!built) {
            std::fprintf(stderr, "error: cannot index '%s'\n",
                         qPrintable(library));
            return 1;
        }
        if (!index.save(cache))
            std::fprintf(stderr, "warning: could not write index cache '%s'\n",
                         qPrintable(cache));
    }
    const qint64 indexMs = timer.elapsed();

    timer.restart();
    std::vector<core::SearchResult> results;
    int lastDone = -1;
    const auto report = [&](int done, int total) {
        if (total > 0 && done != lastDone
            && (done % 16 == 0 || done == total)) {
            lastDone = done;
            std::fprintf(stderr, "\rcomparing %d/%d   ", done, total);
            std::fflush(stderr);
        }
        return true;
    };
    const bool searched =
        index.searchSemantic(query, embedder, threshold, results, report);
    if (!searched) {
        std::fprintf(stderr, "error: cannot read query '%s'\n",
                     qPrintable(query));
        return 1;
    }
    std::fprintf(stderr, "\r");
    const qint64 searchMs = timer.elapsed();

    std::printf("library : %s\n", qPrintable(library));
    std::printf("model   : %s\n", qPrintable(model));
    std::printf("indexed : %zu images%s, %d skipped  (%lld ms, %s)\n",
                size_t(index.size()),
                loaded ? " (cached)" : "",
                index.errorCount(), static_cast<long long>(indexMs),
                loaded ? "load" : "build");
    std::printf("query   : %s\n", qPrintable(query));
    std::printf("load    : %lld ms\n", static_cast<long long>(loadMs));
    std::printf("search  : %lld ms, %zu results above %.0f%% threshold\n\n",
                static_cast<long long>(searchMs), results.size(),
                threshold * 100.0);
    std::printf(" rank   score  path\n");

    for (size_t i = 0; i < results.size(); ++i)
        std::printf("%5zu  %6.3f  %s\n", i + 1, results[i].score,
                    qPrintable(results[i].relPath));
    return 0;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("LucidGrasp"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lucidgrasp.local"));
    QCoreApplication::setApplicationName(QStringLiteral("LucidGrasp"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LUCIDGRASP_VERSION));

    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    if (args.contains(QLatin1String("--help"))
        || args.contains(QLatin1String("-h"))) {
        printUsage();
        return 0;
    }

    if (args.contains(QLatin1String("--reset-legal-agreement"))) {
        QSettings agreement(QSettings::IniFormat, QSettings::UserScope,
                            QCoreApplication::organizationName(),
                            QCoreApplication::applicationName());
        agreement.remove(QStringLiteral("legal/agreed"));
        agreement.sync();
    }

    if (args.contains(QLatin1String("--selftest"))) {
        QCoreApplication app(argc, argv);
        return runSelfTest() == 0 ? 0 : 1;
    }

    if (args.contains(QLatin1String("--cli"))) {
        QCoreApplication app(argc, argv);
        return runCli(args);
    }

    if (args.contains(QLatin1String("--similar"))) {
        QCoreApplication app(argc, argv);
        return runSimilarSearch(args);
    }

#if defined(Q_OS_LINUX)
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(
        QStringLiteral("io.github.dialga_cmd.LucidGrasp"));
    MainWindow window;
    window.resize(1100, 700);
    window.show();
    return app.exec();
}
