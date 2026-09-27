#pragma once

#include "core/index.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <QElapsedTimer>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QProgressBar;
class QSpinBox;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void browseLibrary();
    void browseQuery();
    void startIndex();
    void stopIndex();
    void startSearch();
    void stopSearch();
    void openResult(QListWidgetItem* item);

private:
    void tryLoadIndex(const QString& dir);
    void onIndexProgress(int done, int total, const QString& current);
    void onIndexFinished(bool ok, std::unique_ptr<core::ImageIndex> index);
    void onSearchProgress(int done, int total);
    void onSearchFinished(bool ok, std::vector<core::SearchResult> results);
    void renderResults(const std::vector<core::SearchResult>& results);
    void updateActions();
    void showPreview(const QString& path);
    void setBusy(bool busy);

    QLineEdit* libEdit_ = nullptr;
    QLineEdit* queryEdit_ = nullptr;
    QPushButton* browseLibBtn_ = nullptr;
    QPushButton* browseQueryBtn_ = nullptr;
    QPushButton* indexBtn_ = nullptr;
    QPushButton* searchBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* stats_ = nullptr;
    QListWidget* results_ = nullptr;
    QSpinBox* thresholdSpin_ = nullptr;

    // Read by the search worker while it runs, so the UI must not mutate or
    // replace it until onSearchFinished has run.
    core::ImageIndex index_;
    std::thread worker_;
    std::thread searchWorker_;
    std::atomic<bool> cancel_{false};
    QElapsedTimer searchTimer_;
    bool indexing_ = false;
    bool searching_ = false;
};
