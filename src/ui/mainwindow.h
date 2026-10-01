#pragma once

#include "app/update_checker.h"
#include "core/embedder_builder.h"
#include "core/index.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPalette>

class QAction;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QProgressBar;
class QShowEvent;
class QSpinBox;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void browseLibrary();
    void browseQuery();
    void startIndex();
    void stopIndex();
    void startSearch();
    void stopSearch();
    void openResult(QListWidgetItem* item);
    void toggleTheme();
    void onUpdateCheckFinished(app::CheckOutcome outcome);

private:
    void tryLoadIndex(const QString& dir);
    void onIndexProgress(int done, int total, const QString& current);
    void startEmbedBuild();
    void stopEmbedBuild();
    void onEmbedProgress(int done, int total, const QString& current);
    void onEmbedFinished();
    // Rebuilds the Similar-mode controls: model presence, cache coverage, and
    // whether the build button still has work to do. Read-only over the index,
    // so it is called whenever any of its inputs can have changed.
    void refreshSimilarUi();
    bool similarSelected() const;
    void onIndexFinished(bool ok, std::unique_ptr<core::ImageIndex> index,
                         const QString& error);
    void onSearchProgress(int done, int total);
    void onSearchFinished(bool ok, std::vector<core::SearchResult> results,
                          const QString& error);
    void renderResults(const std::vector<core::SearchResult>& results);
    void updateActions();
    void showPreview(const QString& path);
    void setBusy(bool busy);

    // Whether the desktop is currently asking for a dark palette. Consulted
    // once at startup only, to pick the initial theme; after that the toggle
    // is the sole authority.
    bool systemPrefersDark() const;
    void setDark(bool dark);
    void applyTheme();
    // Pushes the active theme onto a widget that is not a descendant of this
    // window. Dialogs are top-level and so inherit neither the stylesheet nor
    // the palette; without this they stay on the platform defaults, which is a
    // white file dialog inside a dark app.
    void applyThemeTo(QWidget* target) const;
    QString askForDirectory(const QString& title);
    QString askForFile(const QString& title, const QString& filter);
    void updateThemeGlyph();
    // Builds the menu bar. Its real job is giving the update check a permanent
    // home: without somewhere to undo the opt-out, "never check again" is a
    // one-way door that can only be reopened by reinstalling.
    void buildMenus();
    void showUpdateDialog(const QString &tag, const QString &url,
                          const QString &notes);
    // Pins the two control panels to a common height. The Library box is
    // naturally much shorter than the Query box, and stacked in a vertical
    // layout they keep their own size hints, so without this they sit ragged.
    void equalizePanelHeights();

    QGroupBox* libGroup_ = nullptr;
    QGroupBox* queryGroup_ = nullptr;
    // The desktop's palette, captured once at startup and never read from the
    // application palette again. applyTheme() installs the derived theme as the
    // application palette so that dialogs inherit it, which means reading
    // QGuiApplication::palette() later would hand back our own output as if it
    // were the desktop's -- and each toggle would then invert the previous
    // toggle instead of the real thing, walking the colours off the desktop's
    // entirely and never returning to them.
    QPalette desktopPalette_;
    // The panel heights are measured once, on first show, and then locked.
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
    QSpinBox* thresholdSpin_ = nullptr;
    QPushButton* themeToggleBtn_ = nullptr;
    // Similar mode. Hidden entirely in a build without ONNX Runtime, since
    // there the mode does not exist rather than merely being unavailable.
    QComboBox* modeCombo_ = nullptr;
    QLabel* threshLabel_ = nullptr;
    QPushButton* embedBtn_ = nullptr;
    QLabel* embedNote_ = nullptr;
    // A value, not a pointer: the worker pool has to be cancelled and joined
    // before the widgets it reports into are destroyed, and a member makes that
    // the language's job rather than a delete in the right place in the
    // destructor.
    core::EmbeddingBuilder embedder_;
    // The File menu's library and query actions. Held as members so
    // updateActions() can disable them for the same window a search worker is
    // reading index_: browseLibrary() -> tryLoadIndex() replaces the vector
    // underneath the worker, and the plain buttons were not the only way to
    // reach that slot (Ctrl+I worked while the button was disabled).
    QAction* indexAction_ = nullptr;
    QAction* queryAction_ = nullptr;
    app::UpdateChecker *updates_ = nullptr;
    // The "Check for Updates Automatically" menu item. Kept so the opt-out can
    // be undone, and so the item's checkmark can be cleared when the dialog's
    // "never" button is pressed.
    QAction *autoUpdateAction_ = nullptr;
    // True between a user-requested check and its result, so a failure that
    // happened on their command gets an answer and one that happened on the
    // automatic schedule stays silent.
    bool updateCheckManual_ = false;
    bool darkMode_ = false;

    // Read by the search worker while it runs, so the UI must not mutate or
    // replace it until onSearchFinished has run.
    core::ImageIndex index_;
    std::thread worker_;
    std::thread searchWorker_;
    std::atomic<bool> cancel_{false};
    QElapsedTimer searchTimer_;
    bool indexing_ = false;
    bool searching_ = false;
    // True while a Similar-mode embedding build is running. It holds index_ only
    // to read file hashes, but it writes the cache that the next Similar search
    // will read, so it locks the same controls a search does.
    bool embedding_ = false;
};
