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
    setWindowTitle(QStringLiteral("Image Similarity Search"));

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
    const QString path = core::defaultIndexPath(dir);
    if (QFile::exists(path) && index_.load(path) && !index_.empty()) {
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
    const QString root = libEdit_->text();
    if (root.isEmpty())
        return;

    indexing_ = true;
    cancel_ = false;
    indexBtn_->setText(QStringLiteral("Stop Index"));
    indexBtn_->setToolTip(QStringLiteral("Stop and discard all data indexed so far"));
    progress_->setValue(0);
    results_->clear();
    updateActions();
    statusBar()->showMessage(QStringLiteral("Indexing…"));

    worker_ = std::thread([this, root] {
        core::ImageIndex idx;
        const bool ok = idx.build(root, [this](const core::BuildProgress& p) {
            QMetaObject::invokeMethod(
                this,
                [this, done = p.done, total = p.total, cur = p.current] {
                    onIndexProgress(done, total, cur);
                },
                Qt::QueuedConnection);
            return !cancel_.load();
        });

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
    if (total > 0) {
        progress_->setValue(done * 100 / total);
        stats_->setText(QStringLiteral("Indexing %1/%2\n%3")
                            .arg(done)
                            .arg(total)
                            .arg(QFileInfo(current).fileName()));
    }
}

void MainWindow::onIndexFinished(bool ok, core::ImageIndex index)
{
    if (worker_.joinable())
        worker_.join();

    indexBtn_->setText(QStringLiteral("Index Library"));
    indexBtn_->setToolTip(QString());
    indexing_ = false;

    if (!ok || cancel_) {
        stats_->setText(QStringLiteral("Indexing cancelled."));
        statusBar()->showMessage(QStringLiteral("Indexing cancelled"), 4000);
        updateActions();
        return;
    }

    progress_->setValue(100);
    index_ = std::move(index);

    const QString idxPath = core::defaultIndexPath(libEdit_->text());
    const bool saved = index_.save(idxPath);

    stats_->setText(
        QStringLiteral("Indexed %1 images (%2 skipped)%3")
            .arg(index_.size())
            .arg(index_.errorCount())
            .arg(saved ? QStringLiteral(", index saved")
                       : QStringLiteral(", cache save failed")));
    statusBar()->showMessage(QStringLiteral("Indexing complete"), 4000);
    updateActions();
}

void MainWindow::stopIndex()
{
    if (!indexing_)
        return;

    cancel_ = true;
    indexBtn_->setText(QStringLiteral("Stopping..."));
    indexBtn_->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("Stopping... partial index will be discarded"), 4000);
    updateActions();
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

    QElapsedTimer timer;
    timer.start();

    std::vector<core::SearchResult> results;
    if (!index_.searchFile(query, 20, results)) {
        QMessageBox::warning(this, QStringLiteral("Error"),
                             QStringLiteral("Could not read query image."));
        return;
    }
    const qint64 ms = timer.elapsed();

    results_->clear();
    for (const auto& r : results) {
        const QPixmap pm(r.absPath);
        QIcon icon(pm.scaled({128, 128}, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation));

        const int pct = int(r.score * 100.0 + 0.5);
        if (pct < thresholdSpin_->value()) continue;
        
        auto* item = new QListWidgetItem(
            icon,
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

    statusBar()->showMessage(
        QStringLiteral("Found %1 results in %2 ms")
            .arg(results.size())
            .arg(ms),
        5000);
}

void MainWindow::openResult(QListWidgetItem* item)
{
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::updateActions()
{
    const bool canIndex = !indexing_ && !libEdit_->text().isEmpty();
    indexBtn_->setEnabled(indexing_ || canIndex);
    browseLibBtn_->setEnabled(!indexing_);
    browseQueryBtn_->setEnabled(!indexing_);
    searchBtn_->setEnabled(!indexing_ && !index_.empty()
                           && !queryEdit_->text().isEmpty());
}
