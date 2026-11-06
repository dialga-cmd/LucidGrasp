#include "ui/key_entry_dialog.h"

#include "app/key_entry_server.h"

#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

KeyEntryDialog::KeyEntryDialog(app::KeyEntryServer* server, QWidget* parent)
    : QDialog(parent)
    , server_(server)
{
    setWindowTitle(tr("Surface scan — API keys"));
    setModal(false);
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 22, 22, 18);
    layout->setSpacing(10);

    heading_ = new QLabel(this);
    QFont headingFont = heading_->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() + 2.5);
    headingFont.setBold(true);
    heading_->setFont(headingFont);

    body_ = new QLabel(this);
    body_->setWordWrap(true);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 0);  // busy, not a percentage
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);

    urlLabel_ = new QLabel(this);
    urlLabel_->setWordWrap(true);
    urlLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    urlLabel_->setOpenExternalLinks(true);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    secondary_ = new QPushButton(this);
    primary_ = new QPushButton(this);
    primary_->setDefault(true);
    buttons->addWidget(secondary_);
    buttons->addWidget(primary_);

    layout->addWidget(heading_);
    layout->addWidget(body_);
    layout->addWidget(progress_);
    layout->addWidget(urlLabel_);
    layout->addSpacing(4);
    layout->addLayout(buttons);

    connect(primary_, &QPushButton::clicked, this, &KeyEntryDialog::onPrimary);
    connect(secondary_, &QPushButton::clicked, this, &KeyEntryDialog::onSecondary);

    showPermission();
}

void KeyEntryDialog::prompt()
{
    if (server_->isRunning())
        showRunning();
    else
        showPermission();

    show();
    raise();
    activateWindow();
}

void KeyEntryDialog::showPermission()
{
    state_ = State::Permission;
    heading_->setText(tr("Start the local key page?"));
    body_->setText(
        tr("LucidGrasp will start a small web server bound to 127.0.0.1 — this "
           "computer only — and open a page in your default browser where you "
           "can paste your API keys.\n\n"
           "Keys are stored in your OS keychain when available, otherwise in "
           "an owner-only settings file. Nothing leaves the machine except the "
           "requests you explicitly validate."));
    progress_->setVisible(false);
    urlLabel_->setVisible(false);
    secondary_->setText(tr("Cancel"));
    secondary_->setVisible(true);
    primary_->setText(tr("Start and open page"));
    primary_->setEnabled(true);
}

void KeyEntryDialog::showStarting()
{
    state_ = State::Starting;
    heading_->setText(tr("Starting the local server…"));
    body_->setText(tr("This only takes a moment."));
    progress_->setVisible(true);
    urlLabel_->setVisible(false);
    secondary_->setVisible(false);
    primary_->setEnabled(false);
}

void KeyEntryDialog::startNow()
{
    QString error;
    if (!server_->start(&error)) {
        showError(error);
        return;
    }
    showRunning();
    openBrowser();
}

void KeyEntryDialog::showRunning()
{
    state_ = State::Running;
    heading_->setText(tr("Key page is ready"));
    body_->setText(
        tr("It should have opened in your browser. If it did not, use the "
           "button below or copy the address."));
    progress_->setVisible(false);
    const QUrl url = server_->url();
    urlLabel_->setText(QStringLiteral("<a href=\"%1\">%1</a>")
                           .arg(url.toString().toHtmlEscaped()));
    urlLabel_->setVisible(true);
    secondary_->setText(tr("Done"));
    secondary_->setVisible(true);
    primary_->setText(tr("Open page in browser"));
    primary_->setEnabled(true);
}

void KeyEntryDialog::showError(const QString& message)
{
    state_ = State::Error;
    heading_->setText(tr("Could not start the local server"));
    body_->setText(message.isEmpty()
                       ? tr("The address 127.0.0.1 could not be bound. Try again.")
                       : message);
    progress_->setVisible(false);
    urlLabel_->setVisible(false);
    secondary_->setText(tr("Close"));
    secondary_->setVisible(true);
    primary_->setText(tr("Try again"));
    primary_->setEnabled(true);
}

void KeyEntryDialog::openBrowser()
{
    QDesktopServices::openUrl(server_->url());
}

void KeyEntryDialog::onPrimary()
{
    switch (state_) {
    case State::Permission:
        // Let the "Starting…" state paint before doing the work, so the user
        // gets feedback even though binding a port is nearly instant.
        showStarting();
        QTimer::singleShot(250, this, &KeyEntryDialog::startNow);
        break;
    case State::Running:
        openBrowser();
        break;
    case State::Error:
        showPermission();
        break;
    case State::Starting:
        break;
    }
}

void KeyEntryDialog::onSecondary()
{
    close();
}
