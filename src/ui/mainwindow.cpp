#include "ui/mainwindow.h"

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QSet>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QSpinBox>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LucidGrasp"));

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);

    // ----- left control panel -----
    auto* left = new QVBoxLayout;

    auto* libGroup = new QGroupBox(QStringLiteral("Library"), central);
    auto* libLayout = new QVBoxLayout(libGroup);
    auto* libRow = new QHBoxLayout;
    libEdit_ = new QLineEdit(libGroup);
    libEdit_->setPlaceholderText(QStringLiteral("Image folder…"));
    libEdit_->setReadOnly(true);
    browseLibBtn_ = new QPushButton(QStringLiteral("Browse"), libGroup);
    libRow->addWidget(libEdit_, 1);
    libRow->addWidget(browseLibBtn_);
    libLayout->addLayout(libRow);

    indexBtn_ = new QPushButton(QStringLiteral("Index Library"), libGroup);
    libLayout->addWidget(indexBtn_);
    progress_ = new QProgressBar(libGroup);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    libLayout->addWidget(progress_);
    left->addWidget(libGroup);

    auto* queryGroup = new QGroupBox(QStringLiteral("Query image"), central);
    auto* queryLayout = new QVBoxLayout(queryGroup);
    auto* queryRow = new QHBoxLayout;
    queryEdit_ = new QLineEdit(queryGroup);
    queryEdit_->setPlaceholderText(QStringLiteral("Image file…"));
    queryEdit_->setReadOnly(true);
    browseQueryBtn_ = new QPushButton(QStringLiteral("Browse"), queryGroup);
    queryRow->addWidget(queryEdit_, 1);
    queryRow->addWidget(browseQueryBtn_);
    queryLayout->addLayout(queryRow);

    preview_ = new QLabel(queryGroup);
    preview_->setMinimumSize(180, 140);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet(
        QStringLiteral("border: 1px dashed #999; color: #777;"));
    preview_->setText(QStringLiteral("no image"));
    queryLayout->addWidget(preview_);

    searchBtn_ = new QPushButton(QStringLiteral("Search"), queryGroup);
    searchBtn_->setDefault(true);
    
    auto* threshRow = new QHBoxLayout;
    threshRow->addWidget(new QLabel(QStringLiteral("Threshold (%):")));
    thresholdSpin_ = new QSpinBox(queryGroup);
    thresholdSpin_->setRange(0, 100);
    thresholdSpin_->setValue(50);
    threshRow->addWidget(thresholdSpin_);
    
    queryLayout->addWidget(searchBtn_);
    queryLayout->addLayout(threshRow);
    left->addWidget(queryGroup);

    stats_ = new QLabel(central);
    stats_->setWordWrap(true);
    stats_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    stats_->setText(QStringLiteral("No library indexed."));
    left->addWidget(stats_);
    left->addStretch(1);

    rootLayout->addLayout(left, 0);

    // ----- results grid -----
    results_ = new QListWidget(central);
    results_->setViewMode(QListView::IconMode);
    results_->setResizeMode(QListView::Adjust);
    results_->setMovement(QListView::Static);
    results_->setIconSize({128, 128});
    results_->setGridSize({170, 200});
    results_->setSpacing(12);
    results_->setWordWrap(true);
    rootLayout->addWidget(results_, 1);

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(browseLibBtn_, &QPushButton::clicked, this,
            &MainWindow::browseLibrary);
    connect(browseQueryBtn_, &QPushButton::clicked, this,
            &MainWindow::browseQuery);
    connect(indexBtn_, &QPushButton::clicked, this, &MainWindow::startIndex);
    connect(searchBtn_, &QPushButton::clicked, this, &MainWindow::startSearch);
    connect(results_, &QListWidget::itemDoubleClicked, this,
            &MainWindow::openResult);

    updateActions();
}

MainWindow::~MainWindow()
{
    cancel_ = true;
    if (worker_.joinable())
        worker_.join();
    if (searchWorker_.joinable())
        searchWorker_.join();
}

void MainWindow::browseLibrary()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select image library"));
    if (dir.isEmpty())
        return;

    libEdit_->setText(dir);
    tryLoadIndex(dir);
}

void MainWindow::tryLoadIndex(const QString& dir)
{
    results_->clear();
    // Fall back to the pre-1.1 cache location so existing installs keep
    // working; the next reindex writes to the new path.
    const QString path = core::defaultIndexPath(dir);
    const QString legacy = core::legacyIndexPath(dir);
    const QString load = QFile::exists(path) ? path : legacy;

    if (QFile::exists(load) && index_.load(load) && !index_.empty()) {
        stats_->setText(QStringLiteral("Loaded cached index: %1 images (%2 skipped)")
                            .arg(index_.size())
                            .arg(index_.errorCount()));
        statusBar()->showMessage(QStringLiteral("Loaded cached index"), 4000);
    } else {
        index_.clear();
        stats_->setText(QStringLiteral("Library selected — click “Index Library”."));
        statusBar()->showMessage(QStringLiteral("No cached index found"), 4000);
    }
    updateActions();
}

void MainWindow::startIndex()
{
    if (indexing_) {
        stopIndex();
        return;
    }
    if (searching_)
        return; // a search holds index_; let it finish or stop it first
    const QString root = libEdit_->text();
    if (root.isEmpty())
        return;

    indexing_ = true;
    cancel_ = false;
    indexBtn_->setText(QStringLiteral("Stop Index"));
    indexBtn_->setToolTip(QStringLiteral("Stop and discard all data indexed so far"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    results_->clear();
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Indexing…"));

    worker_ = std::thread([this, root] {
        auto idx = std::make_unique<core::ImageIndex>();
        const bool ok = idx->build(root, [this](const core::BuildProgress& p) {
            QMetaObject::invokeMethod(
                this,
                [this, done = p.done, total = p.total, cur = p.current] {
                    onIndexProgress(done, total, cur);
                },
                Qt::QueuedConnection);
            return !cancel_.load();
        });

        // Handed over by pointer: the entry vector is no longer deep-copied
        // twice (once into the queued event, once for a by-value parameter).
        QMetaObject::invokeMethod(
            this,
            [this, ok, idx = std::move(idx)]() mutable {
                onIndexFinished(ok, std::move(idx));
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::onIndexProgress(int done, int total, const QString& current)
{
    if (total <= 0) {
        // Discovery phase: the total file count is not known yet.
        progress_->setRange(0, 0); // busy indicator
        stats_->setText(QStringLiteral("Scanning for images…\n%1").arg(current));
        return;
    }
    progress_->setRange(0, 100);
    progress_->setValue(done * 100 / total);
    stats_->setText(QStringLiteral("Indexing %1/%2\n%3")
                        .arg(done)
                        .arg(total)
                        .arg(QFileInfo(current).fileName()));
}

void MainWindow::onIndexFinished(bool ok, std::unique_ptr<core::ImageIndex> index)
{
    if (worker_.joinable())
        worker_.join();

    indexBtn_->setText(QStringLiteral("Index Library"));
    indexBtn_->setToolTip(QString());
    indexing_ = false;

    if (!ok || cancel_ || !index) {
        stats_->setText(QStringLiteral("Indexing cancelled."));
        statusBar()->showMessage(QStringLiteral("Indexing cancelled"), 4000);
        setBusy(false);
        return;
    }

    progress_->setValue(100);
    index_ = std::move(*index);

    const bool saved = index_.save(core::defaultIndexPath(libEdit_->text()));

    stats_->setText(
        QStringLiteral("Indexed %1 images (%2 skipped)%3")
            .arg(index_.size())
            .arg(index_.errorCount())
            .arg(saved ? QStringLiteral(", index saved")
                       : QStringLiteral(", cache save failed")));
    statusBar()->showMessage(QStringLiteral("Indexing complete"), 4000);
    setBusy(false);
}

void MainWindow::stopIndex()
{
    if (!indexing_)
        return;

    cancel_ = true;
    indexBtn_->setText(QStringLiteral("Stopping..."));
    setBusy(true);
    indexBtn_->setEnabled(false); // stay disabled until the worker reports back
    statusBar()->showMessage(QStringLiteral("Stopping... partial index will be discarded"), 4000);
}

void MainWindow::browseQuery()
{
    static const QString filter = [] {
        QSet<QString> exts;
        for (const QByteArray& f : QImageReader::supportedImageFormats())
            exts.insert(QString::fromLatin1(f).toLower());
        exts << QStringLiteral("png") << QStringLiteral("jpg")
             << QStringLiteral("jpeg") << QStringLiteral("bmp")
             << QStringLiteral("gif");

        QStringList list(exts.cbegin(), exts.cend());
        list.sort();

        QString patterns;
        for (const QString& e : list)
            patterns += QStringLiteral(" *.") + e;

        return QStringLiteral("Images (%1);;All files (*)").arg(patterns);
    }();

    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select query image"), QString(), filter);
    if (file.isEmpty())
        return;

    queryEdit_->setText(file);
    showPreview(file);
    updateActions();
}

void MainWindow::showPreview(const QString& path)
{
    const QPixmap pm(path);
    if (pm.isNull()) {
        preview_->setText(QStringLiteral("cannot load"));
        return;
    }
    preview_->setPixmap(pm.scaled(preview_->size(), Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
}

void MainWindow::startSearch()
{
    if (searching_) {
        stopSearch();
        return;
    }
    if (index_.empty()) {
        QMessageBox::information(this, QStringLiteral("No index"),
                                 QStringLiteral("Index a library first."));
        return;
    }
    const QString query = queryEdit_->text();
    if (query.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No query"),
                                 QStringLiteral("Choose a query image first."));
        return;
    }

    searching_ = true;
    cancel_ = false;
    searchBtn_->setText(QStringLiteral("Stop Search"));
    searchBtn_->setToolTip(QStringLiteral("Stop the search"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    results_->clear();
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Searching…"));
    searchTimer_.start();

    // Runs off the GUI thread so the window stays responsive and cancellable
    // no matter how large the library is. index_ is only read here, and the
    // UI is locked out until onSearchFinished, so there is no shared mutation.
    const double threshold = thresholdSpin_->value() / 100.0;
    searchWorker_ = std::thread([this, query, threshold] {
        std::vector<core::SearchResult> results;
        const bool ok = index_.searchFile(
            query, threshold, results, [this](int done, int total) {
                QMetaObject::invokeMethod(
                    this, [this, done, total] { onSearchProgress(done, total); },
                    Qt::QueuedConnection);
                return !cancel_.load();
            });

        QMetaObject::invokeMethod(
            this,
            [this, ok, results = std::move(results)]() mutable {
                onSearchFinished(ok, std::move(results));
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::stopSearch()
{
    if (!searching_)
        return;

    cancel_ = true;
    searchBtn_->setText(QStringLiteral("Stopping..."));
    searchBtn_->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("Stopping..."), 4000);
}

void MainWindow::onSearchProgress(int done, int total)
{
    if (total > 0) {
        progress_->setValue(done * 100 / total);
        stats_->setText(QStringLiteral("Comparing %1/%2").arg(done).arg(total));
    }
}

void MainWindow::onSearchFinished(bool ok, std::vector<core::SearchResult> results)
{
    if (searchWorker_.joinable())
        searchWorker_.join();

    searching_ = false;
    searchBtn_->setText(QStringLiteral("Search"));
    searchBtn_->setToolTip(QString());
    setBusy(false);

    if (!ok || cancel_) {
        stats_->setText(QStringLiteral("Search cancelled."));
        statusBar()->showMessage(QStringLiteral("Search cancelled"), 4000);
        return;
    }
    if (results.empty()) {
        QMessageBox::warning(this, QStringLiteral("Error"),
                             QStringLiteral("Could not read query image."));
        return;
    }

    const qint64 ms = searchTimer_.isValid() ? searchTimer_.elapsed() : 0;
    renderResults(results);
    statusBar()->showMessage(
        QStringLiteral("Found %1 results in %2 ms")
            .arg(results_->count())
            .arg(ms),
        5000);
}

void MainWindow::renderResults(const std::vector<core::SearchResult>& results)
{
    results_->clear();
    for (const auto& r : results) {
        const int pct = int(r.score * 100.0 + 0.5);
        if (pct < thresholdSpin_->value())
            continue;

        // Decode straight to thumbnail size rather than loading the full
        // image, which matters when results can be very large photographs.
        QImageReader reader(r.absPath);
        const QSize native = reader.size();
        if (native.isValid() && !native.isEmpty())
            reader.setScaledSize(native.scaled(QSize(128, 128), Qt::KeepAspectRatio));
        const QImage thumb = reader.read();

        auto* item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(thumb)),
            QStringLiteral("%1%2\n%3")
                .arg(r.exact ? QStringLiteral("EXACT ") : QString())
                .arg(pct)
                .arg(QFileInfo(r.absPath).fileName()));
        item->setData(Qt::UserRole, r.absPath);
        item->setToolTip(r.absPath + QStringLiteral("\nscore: ")
                         + QString::number(r.score, 'f', 3)
                         + (r.exact ? QStringLiteral("\nbyte-identical copy")
                                    : QString()));
        item->setSizeHint({170, 200});
        results_->addItem(item);
    }
}

void MainWindow::setBusy(bool busy)
{
    // While a worker holds index_ or the result set, the inputs that would
    // invalidate them are locked out.
    browseLibBtn_->setEnabled(!busy);
    browseQueryBtn_->setEnabled(!busy);
    thresholdSpin_->setEnabled(!busy);
    updateActions();
}

void MainWindow::openResult(QListWidgetItem* item)
{
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::updateActions()
{
    const bool busy = indexing_ || searching_;
    const bool canIndex = !busy && !libEdit_->text().isEmpty();
    indexBtn_->setEnabled(indexing_ || canIndex);
    browseLibBtn_->setEnabled(!busy);
    browseQueryBtn_->setEnabled(!busy);
    searchBtn_->setEnabled(busy ? searching_ : (!index_.empty() && !queryEdit_->text().isEmpty()));
}
