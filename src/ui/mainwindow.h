#pragma once

#include "app/update_checker.h"
#include "core/background_remover.h"
#include "core/index.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPalette>

class QAction;
class QColor;
class QDragEnterEvent;
class QDropEvent;
class QGroupBox;
class QIcon;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QProgressBar;
class QRadioButton;
class QShowEvent;
class QSpinBox;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Search mode selected in the "Search Options" panel. Visual is the
    // plain feature match; Similar runs the query through background removal
    // first, using one of the two model variants; Semantic ranks by meaning
    // with the embedding model.
    enum class SearchMode {
        Visual,
        SimilarLite,
        SimilarGeneral,
        Semantic,
    };

protected:
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void browseLibrary();
    void browseQuery();
    void startIndex();
    void stopIndex();
    void startSearch();
    void stopSearch();
    void startBackgroundRemoval();
    void openResult(QListWidgetItem* item);
    void revealResult();
    void trashResult();
    void toggleTheme();
    void onUpdateCheckFinished(app::CheckOutcome outcome);
    void searchModeChanged();

private:
    void tryLoadIndex(const QString& dir);
    void onIndexProgress(int done, int total, const QString& current);
    void onIndexFinished(bool ok, std::unique_ptr<core::ImageIndex> index,
                         const QString& error);
    void onSearchProgress(int done, int total);
    void onSearchFinished(bool ok, std::vector<core::SearchResult> results,
                          const QString& error);
    void onBackgroundDone(bool ok, const QString& sourcePath,
                          const QImage& cutout, const QString& error);
    void showBackgroundResult(const QString& sourcePath, const QImage& cutout);
    void renderResults(const std::vector<core::SearchResult>& results);
    void updateActions();
    void updateResultActions();
    QListWidgetItem* currentResult() const;
    void showPreview(const QString& path);
    void setBusy(bool busy);

    bool systemPrefersDark() const;
    void setDark(bool dark);
    void applyTheme();
    void applyThemeTo(QWidget* target) const;
    QString askForDirectory(const QString& title);
    QString askForFile(const QString& title, const QString& filter);
    void updateThemeGlyph();
    void updateResultIcons();
    static QIcon paintTrashGlyph(int logicalSize, const QColor& ink,
                                 const QColor& faded);
    void buildMenus();
    void showUpdateDialog(const QString &tag, const QString &url,
                          const QString &notes);
    void showAboutDialog();
    void showPrivacyDialog();
    void showLegalNoticesDialog();
    void showWelcomeDialog();
    void openExternal(const QString& url);
    void openIssuePage(const QString& title, const QString& body);
    void equalizePanelHeights();

    SearchMode selectedMode() const;
    void applyModeButtons(SearchMode mode);

    QGroupBox* libGroup_ = nullptr;
    QGroupBox* queryGroup_ = nullptr;
    QGroupBox* optionsGroup_ = nullptr;
    QRadioButton* visualRadio_ = nullptr;
    QRadioButton* similarRadio_ = nullptr;
    QRadioButton* liteRadio_ = nullptr;
    QRadioButton* generalRadio_ = nullptr;
    QRadioButton* semanticRadio_ = nullptr;
    SearchMode lastAppliedMode_ = SearchMode::Visual;
    QPalette desktopPalette_;
    bool panelsSized_ = false;
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
    QPushButton* themeToggleBtn_ = nullptr;
    QPushButton* revealBtn_ = nullptr;
    QPushButton* trashBtn_ = nullptr;
    QSpinBox* thresholdSpin_ = nullptr;
    QAction* indexAction_ = nullptr;
    QAction* queryAction_ = nullptr;
    app::UpdateChecker *updates_ = nullptr;
    QAction *autoUpdateAction_ = nullptr;
    bool updateCheckManual_ = false;

    bool darkMode_ = false;

    core::ImageIndex index_;
    std::thread worker_;
    std::thread searchWorker_;
    std::thread bgWorker_;
    std::atomic<bool> cancel_{false};
    QElapsedTimer searchTimer_;
    bool indexing_ = false;
    bool searching_ = false;
    bool bgBusy_ = false;
    core::BackgroundRemover bgRemover_;
    core::SemanticEmbedder semantic_;
    QAction* bgAction_ = nullptr;
};
