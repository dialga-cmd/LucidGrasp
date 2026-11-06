#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace app {
class KeyEntryServer;
}

// The confirmation and status window for the loopback key page. Its whole job
// is to make the invisible visible: the user is told a local server will
// start before it does, sees that it is starting, and only then is the browser
// opened — exactly once. Re-triggering the menu raises this window instead of
// starting another server or opening another tab.
class KeyEntryDialog : public QDialog {
    Q_OBJECT

public:
    explicit KeyEntryDialog(app::KeyEntryServer* server,
                            QWidget* parent = nullptr);

    // Resets to the right state (permission or already-running) and shows.
    void prompt();

private:
    enum class State { Permission, Starting, Running, Error };

    void showPermission();
    void showStarting();
    void showRunning();
    void showError(const QString& message);
    void startNow();
    void openBrowser();

    void onPrimary();
    void onSecondary();

    app::KeyEntryServer* server_ = nullptr;
    State state_ = State::Permission;

    QLabel* heading_ = nullptr;
    QLabel* body_ = nullptr;
    QLabel* urlLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* primary_ = nullptr;
    QPushButton* secondary_ = nullptr;
};
