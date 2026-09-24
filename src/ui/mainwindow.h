#pragma once

#include "core/index.h"

#include <atomic>
#include <thread>

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QProgressBar;

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
    void openResult(QListWidgetItem* item);

private:
    void tryLoadIndex(const QString& dir);
    void onIndexProgress(int done, int total, const QString& current);
    void onIndexFinished(bool ok, core::ImageIndex index);
    void updateActions();
    void showPreview(const QString& path);

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

    core::ImageIndex index_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    bool indexing_ = false;
};
