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
        "Image Similarity Search\n"
        "\n"
        "Usage:\n"
        "  image_search                          launch GUI\n"
        "  image_search --cli <library> <query> [topK]\n"
        "      headless search; reuses cache if present\n"
        "  image_search --cli --reindex <library> <query> [topK]\n"
        "      force a fresh index\n"
        "  image_search --selftest\n"
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
    const int topK = rest.size() == 3 ? rest[2].toInt() : 20;

    core::ImageIndex index;
    QElapsedTimer timer;
    timer.start();

    bool loaded = false;
    const QString cache = core::defaultIndexPath(library);
    if (!reindex && QFileInfo::exists(cache) && index.load(cache)
        && !index.empty()) {
        loaded = true;
    } else {
        if (!index.build(library)) {
            std::fprintf(stderr, "error: cannot index '%s'\n",
                         qPrintable(library));
            return 1;
        }
        index.save(cache);
    }

    const qint64 indexMs = timer.elapsed();

    timer.restart();
    std::vector<core::SearchResult> results;
    if (!index.searchFile(query, topK, results)) {
        std::fprintf(stderr, "error: cannot read query '%s'\n",
                     qPrintable(query));
        return 1;
    }
    const qint64 searchMs = timer.elapsed();

    std::printf("library : %s\n", qPrintable(library));
    std::printf("indexed : %zu images%s, %d skipped  (%lld ms, %s)\n",
                size_t(index.size()),
                loaded ? " (cached)" : "",
                index.errorCount(), static_cast<long long>(indexMs),
                loaded ? "load" : "build");
    std::printf("query   : %s\n", qPrintable(query));
    std::printf("search  : %lld ms, top %zu of results\n\n",
                static_cast<long long>(searchMs), results.size());
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

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); // force Qt widget dialog (GTK3 native rejects files)
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Image Similarity Search"));
    MainWindow window;
    window.resize(1100, 700);
    window.show();
    return app.exec();
}
