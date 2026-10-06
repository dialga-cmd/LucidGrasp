#pragma once

#include "app/update_checker.h"
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
class QGroupBox;
class QIcon;
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
    void revealResult();
    void trashResult();
    void toggleTheme();
    void onUpdateCheckFinished(app::CheckOutcome outcome);

private:
    void tryLoadIndex(const QString& dir);
    void onIndexProgress(int done, int total, const QString& current);
    void onIndexFinished(bool ok, std::unique_ptr<core::ImageIndex> index,
                         const QString& error);
    void onSearchProgress(int done, int total);
    void onSearchFinished(bool ok, std::vector<core::SearchResult> results,
                          const QString& error);
    void renderResults(const std::vector<core::SearchResult>& results);
    void updateActions();
    // Enables or disables the reveal/trash pair. Split from updateActions()
    // because it also depends on the grid selection, which changes on its own
    // schedule -- on click, and after a delete takes the row away.
    void updateResultActions();
    // The selected result, or null when nothing usable is selected.
    QListWidgetItem* currentResult() const;
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
    // Repaints the two top-bar tool icons. Kept apart from updateThemeGlyph()
    // because they follow the palette for a different reason: the bin's ink is
    // sampled from it, while the toggle's glyph is a fixed shape whose colour
    // only changes with the mode. Both have to run on a theme change.
    void updateResultIcons();
    // Draws the bin twice, in `ink` and in `faded`, and registers them as the
    // Normal and Disabled states of one QIcon.
    static QIcon paintTrashGlyph(int logicalSize, const QColor& ink,
                                 const QColor& faded);
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
    // Top-bar controls, in the order they appear: theme, reveal, delete. The
    // latter two act on the current selection rather than on a fixed image,
    // which is what makes them usable against a grid holding a whole result set.
    QPushButton* themeToggleBtn_ = nullptr;
    QPushButton* revealBtn_ = nullptr;
    QPushButton* trashBtn_ = nullptr;
    QSpinBox* thresholdSpin_ = nullptr;
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
};
