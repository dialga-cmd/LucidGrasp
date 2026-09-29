#include <cstdio>
#include <cstring>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include "core/index.h"
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
        "      headless search; reuses cache if present\n"
        "  LucidGrasp --cli --reindex <library> <query> [threshold%%]\n"
        "      force a fresh index\n"
        "  LucidGrasp --selftest\n"
        "      build a synthetic corpus and verify ranking\n");
}

int runCli(const QStringList& args)
{
    // args[0] = program, args[1] = --cli
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

    const QString library = rest[0];
    const QString query = rest[1];
    // Strict parse. toDouble() alone silently turns "abc" into 0.0, which
    // reads as "threshold 0% — return every image in the library", and a value
    // outside 0-100 would be passed through to the search unclamped. Reject
    // both instead of guessing what the user meant.
    double threshold = 0.5;
    if (rest.size() == 3) {
        bool parsed = false;
        const double pct = rest[2].toDouble(&parsed);
        if (!parsed || pct < 0.0 || pct > 100.0) {
            std::fprintf(stderr,
                         "error: threshold must be a number from 0 to 100, "
                         "got '%s'\n",
                         qPrintable(rest[2]));
            return 2;
        }
        threshold = pct / 100.0;
    }

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
            // total == 0 means the file count is still being discovered.
            // done * 100 in int overflows past ~21.4M files; the percentage is
            // the only use, so the guard lives on this one expression.
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
        // A read-only or missing cache directory must not pass silently.
        if (!index.save(cache))
            std::fprintf(stderr, "warning: could not write index cache '%s'\n",
                         qPrintable(cache));
    }

    const qint64 indexMs = timer.elapsed();

    timer.restart();
    std::vector<core::SearchResult> results;
    int lastDone = -1;
    if (!index.searchFile(query, threshold, results,
                          [&](int done, int total) {
                              if (total > 0 && done != lastDone
                                  && (done % 16 == 0 || done == total)) {
                                  lastDone = done;
                                  std::fprintf(stderr, "\rcomparing %d/%d   ",
                                               done, total);
                                  std::fflush(stderr);
                              }
                              return true;
                          })) {
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

} // namespace

int main(int argc, char* argv[])
{
    // Must be set before any index path is resolved: the cache location is
    // derived from the application data directory.
    QCoreApplication::setOrganizationName(QStringLiteral("LucidGrasp"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lucidgrasp.local"));
    QCoreApplication::setApplicationName(QStringLiteral("LucidGrasp"));
    // Supplied by CMake from project(... VERSION ...). Do not hardcode a copy
    // here: the update checker compares this string against the release tag,
    // so a stale literal means every user is told to update forever.
    QCoreApplication::setApplicationVersion(QStringLiteral(LUCIDGRASP_VERSION));

    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    if (args.contains(QLatin1String("--help"))
        || args.contains(QLatin1String("-h"))) {
        printUsage();
        return 0;
    }

    if (args.contains(QLatin1String("--selftest"))) {
        QCoreApplication app(argc, argv);
        return runSelfTest() == 0 ? 0 : 1;
    }

    if (args.contains(QLatin1String("--cli"))) {
        QCoreApplication app(argc, argv);
        return runCli(args);
    }

#if defined(Q_OS_LINUX)
    // Scoped to Linux on purpose. GTK3's native file dialog rejects files, so
    // Qt's own is used there. This used to be a global attribute, which also
    // suppressed the platform dialog on Windows and macOS -- the one place the
    // OS can legitimately own how a file picker looks, and the reason the OS
    // palette was not reaching it on those two platforms.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication app(argc, argv);
    MainWindow window;
    window.resize(1100, 700);
    window.show();
    return app.exec();
}
