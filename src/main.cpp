#include <cstdio>
#include <cstring>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QSettings>
#include <QTextStream>

#include "core/background_remover.h"
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
        "      build a synthetic corpus and verify ranking\n"
        "  LucidGrasp --bg-remove <input> <output> [model.onnx]\n"
        "      remove the background of an image with an ONNX model;\n"
        "      when <input> is a folder every image inside is processed\n"
        "      (recursively) into <output> using one model load;\n"
        "      the model defaults to the standard model location\n"
        "  LucidGrasp --reset-legal-agreement\n"
        "      clear the stored first-run agreement; the Welcome\n"
        "      dialog is shown again and must be agreed to enter\n");
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

    const QString library = rest[0];
    const QString query = rest[1];
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

int runBackgroundBatch(core::BackgroundRemover& remover, const QString& model,
                       qint64 loadMs, const QString& input,
                       const QString& output);

int runBackgroundRemoval(const QStringList& args)
{
    QStringList rest;
    bool seen = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--bg-remove"))
            seen = true;
        else if (seen)
            rest.append(args[i]);
    }

    if (rest.size() < 2 || rest.size() > 3) {
        printUsage();
        return 2;
    }

    const QString input = rest[0];
    const QString output = rest[1];
    QString model =
        rest.size() == 3 ? rest[2] : core::defaultModelPath();
    if (model.isEmpty()) {
        std::fprintf(stderr,
                     "error: no background model found; run ./fetch-model.sh "
                     "or pass a model path as the third argument\n");
        return 1;
    }

    core::BackgroundRemover remover;
    QString error;
    QElapsedTimer timer;
    timer.start();
    if (!remover.loadModel(model, &error)) {
        std::fprintf(stderr, "error: cannot load model: %s\n",
                     qPrintable(error));
        return 1;
    }
    const qint64 loadMs = timer.elapsed();

    const QFileInfo inputInfo(input);
    if (inputInfo.isDir())
        return runBackgroundBatch(remover, model, loadMs, input, output);

    const QImage source(input);
    if (source.isNull()) {
        std::fprintf(stderr, "error: cannot read '%s'\n",
                     qPrintable(input));
        return 1;
    }

    timer.restart();
    QImage cutout;
    if (!remover.removeBackground(source, &cutout, &error)) {
        std::fprintf(stderr, "error: %s\n", qPrintable(error));
        return 1;
    }
    const qint64 inferMs = timer.elapsed();

    if (!cutout.save(output, "PNG")) {
        std::fprintf(stderr, "error: cannot write '%s'\n",
                     qPrintable(output));
        return 1;
    }

    std::printf("model    : %s\n", qPrintable(model));
    std::printf("input    : %dx%d\n", source.width(), source.height());
    std::printf("load     : %lld ms, inference %lld ms\n",
                static_cast<long long>(loadMs),
                static_cast<long long>(inferMs));
    std::printf("cutout   : %dx%d saved to %s\n", cutout.width(),
                cutout.height(), qPrintable(output));
    return 0;
}

int runBackgroundBatch(core::BackgroundRemover& remover, const QString& model,
                       qint64 loadMs, const QString& input,
                       const QString& output)
{
    const QDir inputDir(input);
    QDir().mkpath(output);
    if (!QFileInfo::exists(output) || !QFileInfo(output).isDir()) {
        std::fprintf(stderr, "error: cannot create output folder '%s'\n",
                     qPrintable(output));
        return 1;
    }

    QStringList nameFilters;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        nameFilters << QStringLiteral("*.%1")
                           .arg(QString::fromLatin1(format));
    nameFilters.sort();

    QStringList rels;
    QDirIterator it(input, nameFilters, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        rels << inputDir.relativeFilePath(it.filePath());
    }
    rels.sort();
    if (rels.isEmpty()) {
        std::fprintf(stderr, "error: no images found under '%s'\n",
                     qPrintable(input));
        return 1;
    }

    std::printf("model    : %s\n", qPrintable(model));
    std::printf("input    : %s (%d images)\n", qPrintable(input),
               static_cast<int>(rels.size()));
    std::printf("load     : %lld ms\n", static_cast<long long>(loadMs));

    QElapsedTimer batchTimer;
    batchTimer.start();
    QElapsedTimer timer;
    int done = 0;
    int failed = 0;
    for (const QString& rel : rels) {
        ++done;
        const QString src = inputDir.filePath(rel);
        timer.restart();
        QImage cutout;
        QString err;
        const bool ok = remover.removeBackground(QImage(src), &cutout, &err);
        const qint64 ms = timer.elapsed();
        if (ok) {
            const QString base =
                QFileInfo(rel).completeBaseName() + QStringLiteral(".png");
            const QString outRel =
                QFileInfo(rel).path() == QLatin1String(".")
                    ? base
                    : QFileInfo(rel).path() + QLatin1Char('/') + base;
            const QString outPath = QDir(output).filePath(outRel);
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            if (cutout.save(outPath, "PNG")) {
                std::printf("[%d/%d] %s ok (%.1f s)\n", done, static_cast<int>(rels.size()),
                            qPrintable(rel), ms / 1000.0);
                std::fflush(stdout);
                continue;
            }
            err = QStringLiteral("cannot write %1").arg(outPath);
        }
        ++failed;
        std::fprintf(stderr, "[%d/%d] %s failed: %s\n", done,
             static_cast<int>(rels.size()),
                     qPrintable(rel), qPrintable(err));
    }

    std::printf("batch    : %d ok, %d failed, %lld ms total\n", done - failed,
                failed, static_cast<long long>(batchTimer.elapsed()));
    return failed ? 1 : 0;
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

    if (args.contains(QLatin1String("--bg-remove"))) {
        QCoreApplication app(argc, argv);
        return runBackgroundRemoval(args);
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
