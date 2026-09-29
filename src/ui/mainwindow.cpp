#include "ui/mainwindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QSpinBox>
#include <QStatusBar>
// QGuiApplication forward-declares QStyleHints, so styleHints()->anything()
// is a call on an incomplete type unless this is included. The class has
// existed since Qt 5; only colorScheme() on it is version gated.
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

// The sun and the moon are painted rather than pulled from the desktop icon
// theme. The freedesktop names for these -- "weather-clear" and
// "weather-clear-night" -- resolve on Linux but come back empty on Windows, and
// the toggle would then draw with no icon at all. Painting keeps one
// implementation on every platform and lets the glyph take the accent colour of
// whichever theme is active.
QIcon paintThemeGlyph(bool dark, const QColor &colour, int logicalSize) {
  const qreal dpr = qApp->devicePixelRatio();
  const int px = qMax(1, qRound(logicalSize * dpr));
  QPixmap pixmap(px, px);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);

  const qreal s = logicalSize;
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.scale(dpr, dpr);

  if (dark) {
    // Crescent. subtracted() rather than an OddEvenFill two-ellipse path:
    // with the latter, the parts of the bite that spill outside the main
    // disc have odd winding too, so they get painted and the result is a
    // symmetric difference, not a crescent.
    //
    // The disc is then inset and nudged right, because a crescent's ink
    // always crowds into the lower left. These values were found by
    // rendering candidates and centring the ink's *bounding box*, which is
    // what the eye reads as "centred" -- a crescent's centroid can never
    // sit on the canvas centre no matter how it is drawn.
    const qreal ox = s * 0.11, oy = s * 0.04, d = s * 0.92;
    QPainterPath disc, bite;
    disc.addEllipse(QRectF(ox, oy, d, d));
    bite.addEllipse(QRectF(ox + d * 0.28, oy - d * 0.14, d, d));
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(disc.subtracted(bite));
  } else {
    // Sun: a disc ringed by eight rays. The stroke is kept light and the
    // rays start clear of the disc, because at the 17px this is drawn at,
    // anything heavier fuses the eight rays into a single smear.
    const QRectF core(s * 0.32, s * 0.32, s * 0.36, s * 0.36);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawEllipse(core);

    const QPointF mid = core.center();
    const qreal inner = s * 0.32, outer = s * 0.46;
    p.setPen(QPen(colour, qMax(1.0, s * 0.06), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 8; ++i) {
      const qreal a = i * kPi / 4.0; // first ray points straight up
      const qreal dx = std::cos(a), dy = std::sin(a);
      p.drawLine(QPointF(mid.x() + dx * inner, mid.y() + dy * inner),
                 QPointF(mid.x() + dx * outer, mid.y() + dy * outer));
    }
  }
  p.end();
  return QIcon(pixmap);
}

// ----- colours taken from the desktop ---------------------------------------
//
// The palette the platform hands us is the only place a colour can come from if
// the desktop is to decide the theme. Worth being precise about what that
// actually yields: it is genuinely native on Windows, follows the GTK theme on
// Linux when a platform-theme plugin is loaded, and on macOS it is Qt's own
// approximation of the system colours rather than the real ones. "Let the OS
// choose" is only ever as good as what the OS reports.

double relativeLuminance(const QColor &c) {
  auto channel = [](int v) {
    const double x = v / 255.0;
    return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) +
         0.0722 * channel(c.blue());
}

double contrastRatio(const QColor &a, const QColor &b) {
  const double la = relativeLuminance(a), lb = relativeLuminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor mixToward(const QColor &from, const QColor &to, double t) {
  return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                          from.greenF() + (to.greenF() - from.greenF()) * t,
                          from.blueF() + (to.blueF() - from.blueF()) * t);
}

// The scheme the desktop is not currently using, built by inverting lightness
// and carrying hue and saturation through. Qt only reports and holds the
// current scheme, so the other one has to be constructed; inverting keeps the
// desktop's accent recognisable instead of substituting a guess for it.
QPalette invertedPalette(const QPalette &in) {
  constexpr double kLo = 5.0, kHi = 94.0;
  QPalette out = in;
  for (int g = 0; g <= int(QPalette::Disabled); ++g)
    for (int r = 0; r < int(QPalette::NColorRoles); ++r) {
      const QColor c = in.color(QPalette::ColorGroup(g), QPalette::ColorRole(r));
      out.setColor(QPalette::ColorGroup(g), QPalette::ColorRole(r),
                   QColor::fromHslF(c.hueF(), c.saturationF(),
                                    std::clamp(1.0 - c.lightnessF(), kLo / 100.0,
                                               kHi / 100.0)));
    }
  return out;
}

struct ThemeColours {
  QColor window;  // app background
  QColor panel;   // group boxes
  QColor field;   // inputs
  QColor text;
  QColor muted;   // secondary and disabled text
  QColor border;
  QColor accent;         // primary button
  QColor onAccent;       // its label, per state: see below
  QColor onAccentHover;  //
  QColor onAccentPressed;      //
  QColor accentHover;          //
  QColor accentPressed;        // shades rather than the neutral hover, or the
  QColor disabled;             // button turns grey.
  QColor hover;         // neutral hover for controls sitting on @field
  QColor pressed;       //
  QColor itemHover;     // hover for list items, which sit on @panel
  QColor checked;       // selected result cell
  QColor onChecked;
};

ThemeColours coloursFromPalette(const QPalette &p) {
  const QColor text = p.color(QPalette::WindowText);
  const QColor accent = p.color(QPalette::Highlight);
  QColor window = p.color(QPalette::Window);
  const QColor base = p.color(QPalette::Base);

  // The layout sets a card on a background, so the two have to differ. Plenty
  // of themes report the same value for Window and Base, and inverting can
  // collapse them onto the same clamp. When that happens the background is
  // stepped away by the least amount that reads, rather than shipping a flat
  // rectangle with a border drawn around nothing.
  if (contrastRatio(base, window) < 1.02)
    window = mixToward(window, text, 0.05);

  // Cards and inputs deliberately share the content surface, and the 1px
  // border is what separates them -- which is how the desktop draws the same
  // pair of things.
  const QColor panel = base, field = base;

  // Muted text. PlaceholderText is the role meant to supply this, but several
  // styles leave it identical to the text colour, and QColor::operator!= also
  // compares the colour spec, so an unstyled placeholder compares unequal to
  // black text and would be taken at face value -- giving muted labels that
  // are simply full-strength black. So the role is used only when it really
  // is muted, and otherwise the text colour is washed toward the background
  // as far as the body-text ratio allows.
  QColor muted = text;
  const QColor placeholder = p.color(QPalette::PlaceholderText);
  if (placeholder.rgb() != text.rgb() &&
      contrastRatio(placeholder, panel) < contrastRatio(text, panel) - 0.5 &&
      contrastRatio(placeholder, panel) >= 4.6 &&
      contrastRatio(placeholder, window) >= 4.6) {
    muted = placeholder;
  } else {
    for (double t = 0.02; t <= 0.95; t += 0.01) {
      const QColor c = mixToward(text, window, t);
      if (contrastRatio(c, panel) >= 4.6 && contrastRatio(c, window) >= 4.6)
        muted = c;
    }
  }

  // Pressing a button darkens the accent, which walks it across the luminance
  // at which a readable label has to flip between black and white. Qt's own
  // blue sits right on that crossover: white reads 3.7:1 on the accent at
  // rest, black 3.6:1 on the pressed state, so no single label clears the ratio
  // across all three. Hence one label per state rather than one per button --
  // the desktop's own label first, then black and white, whichever measures
  // best. Most accents are dark enough that all three agree and nothing flips;
  // it is the lighter desktop accents that need this.
  const QColor accentHover = mixToward(accent, text, 0.12);
  const QColor accentPressed = mixToward(accent, text, 0.24);
  const QColor labelOptions[] = {p.color(QPalette::HighlightedText),
                                 QColor(Qt::white), QColor(Qt::black), text};
  auto bestLabelOn = [&labelOptions](const QColor &bg) {
    QColor best = labelOptions[0];
    double top = -1.0;
    for (const QColor &c : labelOptions) {
      const double r = contrastRatio(c, bg);
      if (r > top) {
        top = r;
        best = c;
      }
    }
    return best;
  };

  const QColor checked = mixToward(panel, accent, 0.20);
  const QColor onCheckedOptions[] = {text, QColor(Qt::white), QColor(Qt::black)};
  QColor onChecked = text;
  double onCheckedRatio = -1.0;
  for (const QColor &c : onCheckedOptions) {
    const double r = contrastRatio(c, checked);
    if (r > onCheckedRatio) {
      onCheckedRatio = r;
      onChecked = c;
    }
  }

  return ThemeColours{
      window,
      panel,
      field,
      text,
      muted,
      p.color(QPalette::Mid),
      accent,
      bestLabelOn(accent),
      bestLabelOn(accentHover),
      bestLabelOn(accentPressed),
      accentHover,
      accentPressed,
      mixToward(panel, text, 0.05),
      mixToward(field, text, 0.07),
      mixToward(field, text, 0.13),
      mixToward(panel, text, 0.07),
      checked,
      onChecked};
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), desktopPalette_(QGuiApplication::palette()) {
  setWindowTitle(QStringLiteral("LucidGrasp"));

  auto *central = new QWidget(this);
  central->setObjectName(QStringLiteral("centralWidget"));
  auto *rootLayout = new QVBoxLayout(central);

  // ----- top bar: title on the left, theme control on the right -----
  auto *topBar = new QHBoxLayout;
  topBar->setContentsMargins(0, 0, 0, 0);

  auto *title = new QLabel(QStringLiteral("LucidGrasp"), central);
  title->setObjectName(QStringLiteral("titleLabel"));
  topBar->addWidget(title);
  topBar->addStretch(1);

  // Square, icon-only: the glyph shows the theme you are in, the tooltip says
  // what clicking does, and the square footprint keeps the bar compact.
  themeToggleBtn_ = new QPushButton(central);
  themeToggleBtn_->setObjectName(QStringLiteral("themeToggleBtn"));
  themeToggleBtn_->setCursor(Qt::PointingHandCursor);

  // Sized off the font so it lines up with the buttons in the panels below
  // and still grows if the user raises their system font size.
  const int side = fontMetrics().height() + 12;
  themeToggleBtn_->setIconSize(QSize(side - 12, side - 12));
  themeToggleBtn_->setFixedSize(side, side);
  topBar->addWidget(themeToggleBtn_);

  rootLayout->addLayout(topBar);

  // AFTER
  auto *bodyLayout = new QHBoxLayout;

  // ----- left control panel -----
  auto *leftWidget = new QWidget(central);
  leftWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  auto *left = new QVBoxLayout(leftWidget);
  left->setContentsMargins(0, 0, 0, 0);

  libGroup_ = new QGroupBox(QStringLiteral("Library"), central);
  libGroup_->setObjectName(QStringLiteral("libGroup"));
  auto *libLayout = new QVBoxLayout(libGroup_);
  auto *libRow = new QHBoxLayout;
  libEdit_ = new QLineEdit(libGroup_);
  libEdit_->setObjectName(QStringLiteral("libEdit"));
  libEdit_->setPlaceholderText(QStringLiteral("Image folder…"));
  libEdit_->setReadOnly(true);
  browseLibBtn_ = new QPushButton(QStringLiteral("Browse"), libGroup_);
  libRow->addWidget(libEdit_, 1);
  libRow->addWidget(browseLibBtn_);
  libLayout->addLayout(libRow);

  indexBtn_ = new QPushButton(QStringLiteral("Index Library"), libGroup_);
  indexBtn_->setObjectName(QStringLiteral("indexBtn"));
  libLayout->addWidget(indexBtn_);
  progress_ = new QProgressBar(libGroup_);
  progress_->setRange(0, 100);
  progress_->setValue(0);
  libLayout->addWidget(progress_);
  left->addWidget(libGroup_);

  queryGroup_ = new QGroupBox(QStringLiteral("Query image"), central);
  queryGroup_->setObjectName(QStringLiteral("queryGroup"));
  auto *queryLayout = new QVBoxLayout(queryGroup_);
  auto *queryRow = new QHBoxLayout;
  queryEdit_ = new QLineEdit(queryGroup_);
  queryEdit_->setObjectName(QStringLiteral("queryEdit"));
  queryEdit_->setPlaceholderText(QStringLiteral("Image file…"));
  queryEdit_->setReadOnly(true);
  browseQueryBtn_ = new QPushButton(QStringLiteral("Browse"), queryGroup_);
  queryRow->addWidget(queryEdit_, 1);
  queryRow->addWidget(browseQueryBtn_);
  queryLayout->addLayout(queryRow);

  preview_ = new QLabel(queryGroup_);
  preview_->setObjectName(QStringLiteral("previewLabel"));
  preview_->setMinimumSize(180, 140);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setText(QStringLiteral("no image"));
  queryLayout->addWidget(preview_);

  searchBtn_ = new QPushButton(QStringLiteral("Search"), queryGroup_);
  searchBtn_->setObjectName(QStringLiteral("searchBtn"));
  searchBtn_->setDefault(true);

  auto *threshRow = new QHBoxLayout;
  threshRow->addWidget(new QLabel(QStringLiteral("Threshold (%):")));
  thresholdSpin_ = new QSpinBox(queryGroup_);
  thresholdSpin_->setRange(0, 100);
  thresholdSpin_->setValue(50);
  threshRow->addWidget(thresholdSpin_);

  queryLayout->addWidget(searchBtn_);
  queryLayout->addLayout(threshRow);
  left->addWidget(queryGroup_);

  stats_ = new QLabel(central);
  stats_->setObjectName(QStringLiteral("statsLabel"));
  stats_->setWordWrap(true);
  stats_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  stats_->setText(QStringLiteral("No library indexed."));
  left->addWidget(stats_);
  left->addStretch(1);

  bodyLayout->addWidget(leftWidget, 0, Qt::AlignTop);

  // ----- results grid -----
  results_ = new QListWidget(central);
  results_->setObjectName(QStringLiteral("resultsView"));
  results_->setViewMode(QListView::IconMode);
  results_->setResizeMode(QListView::Adjust);
  results_->setMovement(QListView::Static);
  results_->setIconSize({128, 128});
  results_->setGridSize({170, 200});
  results_->setSpacing(12);
  results_->setWordWrap(true);
  bodyLayout->addWidget(results_, 1);

  rootLayout->addLayout(bodyLayout, 1);

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
  connect(themeToggleBtn_, &QPushButton::clicked, this,
          &MainWindow::toggleTheme);

  // Before the menus, which read the stored opt-out to set their checkmark.
  updates_ = new app::UpdateChecker(this);
  connect(updates_, &app::UpdateChecker::updateAvailable, this,
          [this](const QString &tag, const QString &url,
                 const QString &notes) { showUpdateDialog(tag, url, notes); });
  connect(updates_, &app::UpdateChecker::finished, this,
          &MainWindow::onUpdateCheckFinished);

  buildMenus();

  // Seed the initial theme from the desktop, then hand over to the toggle.
  // Honouring the system here avoids flashing a white window at someone whose
  // desktop is dark, and costs nothing because the toggle overrides it.
  darkMode_ = systemPrefersDark();
  applyTheme();
  updateActions();
}

MainWindow::~MainWindow() {
  cancel_ = true;
  if (worker_.joinable())
    worker_.join();
  if (searchWorker_.joinable())
    searchWorker_.join();
}

void MainWindow::buildMenus() {
  QMenuBar *bar = menuBar();

  // File duplicates the two Browse buttons and Quit. Not padding: it puts the
  // keyboard route to the common actions where it belongs, and it is the second
  // half of the point below. Written out rather than using the QMenu::addAction
  // convenience overloads because the receiver-taking form is deprecated in Qt 6.
  QMenu *file = bar->addMenu(tr("&File"));

  // Kept as members because a search worker reads index_ directly and these two
  // actions mutate it (browseLibrary -> tryLoadIndex replaces index_). Disabling
  // only the buttons still left Ctrl+I live during a search, which freed the
  // entry vector under the worker. They are gated alongside the buttons in
  // updateActions(), so the keyboard path and the click path can never diverge.
  indexAction_ = file->addAction(tr("Select &Library..."));
  indexAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
  connect(indexAction_, &QAction::triggered, this, &MainWindow::browseLibrary);

  queryAction_ = file->addAction(tr("Select &Query Image..."));
  queryAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
  connect(queryAction_, &QAction::triggered, this, &MainWindow::browseQuery);

  file->addSeparator();
  QAction *quitAction = file->addAction(tr("E&xit"));
  quitAction->setShortcut(QKeySequence::Quit);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  // Help is where the update check lives, and that is the reason this menu
  // exists. The dialog offers a permanent opt-out, so there has to be a way to
  // take it back: without a permanent entry point, "never check for updates"
  // could only be undone by reinstalling the app.
  QMenu *help = bar->addMenu(tr("&Help"));

  QAction *checkAction = help->addAction(tr("&Check for Updates..."));
  checkAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
  connect(checkAction, &QAction::triggered, this, [this] {
    updateCheckManual_ = true;
    // updates_ is constructed before the menus (see MainWindow()), so it is
    // always live here; the guard used to imply it might not be.
    updates_->checkNow();
  });

  autoUpdateAction_ = help->addAction(tr("Check for Updates &Automatically"));
  autoUpdateAction_->setCheckable(true);
  autoUpdateAction_->setChecked(!updates_->isDisabled());
  connect(autoUpdateAction_, &QAction::toggled, this, [this](bool on) {
    updates_->setDisabled(!on);
  });
}

void MainWindow::showUpdateDialog(const QString &tag, const QString &url,
                                  const QString &notes) {
  auto *box = new QMessageBox(this);
  box->setIcon(QMessageBox::Information);
  box->setWindowTitle(tr("Update available"));
  box->setText(tr("LucidGrasp %1 has been released.\nYou are running %2.")
                   .arg(app::normaliseVersion(tag),
                        app::UpdateChecker::currentVersion()));

  if (!notes.isEmpty()) {
    // Release notes are markdown written for a browser and can be arbitrarily
    // long, so they are trimmed and set as the detail line rather than the
    // main text: that wraps them to the dialog width instead of stretching it.
    constexpr int kMaxNotes = 700;
    QString shown = notes.left(kMaxNotes).trimmed();
    if (notes.size() > kMaxNotes)
      shown += QStringLiteral("\n…");
    box->setInformativeText(shown);
  }

  auto *open = box->addButton(tr("&Open Download Page"), QMessageBox::AcceptRole);
  auto *later = box->addButton(tr("&Not Now"), QMessageBox::RejectRole);
  auto *never =
      box->addButton(tr("Never Check for &Updates"), QMessageBox::DestructiveRole);
  box->setDefaultButton(later);

  // Non-modal on purpose. A modal box over the window would block the user out
  // of the app they just launched, and the notice is never urgent enough to
  // earn that.
  box->setWindowModality(Qt::NonModal);

  connect(open, &QAbstractButton::clicked, box, [box, url] {
    QDesktopServices::openUrl(QUrl(url));
    box->accept();
  });
  connect(later, &QAbstractButton::clicked, box, &QDialog::accept);
  connect(never, &QAbstractButton::clicked, box, [this, box] {
    // Same construction-order guarantee as the menu handlers above.
    updates_->setDisabled(true);
    autoUpdateAction_->setChecked(false);
    box->accept();
  });
  // Each button closes the box above, and closing it with the window X emits
  // finished too, so this is the single teardown path.
  connect(box, &QDialog::finished, box, &QObject::deleteLater);

  box->show();
}

void MainWindow::onUpdateCheckFinished(app::CheckOutcome outcome) {
  // UpdateAvailable is already on screen as a dialog, and Suppressed means the
  // user has said what they want. Neither needs a status-bar line.
  // The manual/automatic distinction applies to one check only, so the flag is
  // consumed here on every outcome. Leaving it set when an update was found
  // (or suppressed) leaked "manual" into the next automatic check, which then
  // complained about an unreachable GitHub exactly like a asked-for check.
  const bool wasManual = updateCheckManual_;
  updateCheckManual_ = false;

  if (outcome != app::CheckOutcome::UpToDate &&
      outcome != app::CheckOutcome::Unreachable)
    return;

  // A failure on the automatic schedule says nothing. Corporate proxies that
  // inspect TLS break this check for whole offices, and a status-bar complaint
  // every day that nobody can act on only teaches people to ignore the bar.
  if (!wasManual)
    return;

  if (outcome == app::CheckOutcome::UpToDate)
    statusBar()->showMessage(
        tr("LucidGrasp %1 is up to date.")
            .arg(app::UpdateChecker::currentVersion()),
        6000);
  else
    statusBar()->showMessage(tr("Could not reach GitHub to check for updates."),
                             6000);
}

// Both pickers are deliberately left on the platform's own dialog wherever one
// exists, so the popup carries the desktop's real colours. Forcing Qt's
// built-in dialog instead was a mistake: it only ever helped after someone had
// manually overridden the theme away from the system, and it cost the macOS
// QuickLook previews and security-scoped bookmarks, and the Windows shell
// dialog, on every single use.
//
// The theme is still pushed onto the instance, because that covers the case
// where no native dialog exists at all. Qt then falls back to its own built-in
// picker, which is a plain top-level window and so inherits neither this
// window's stylesheet nor its palette. Unstyled, it renders as a white dialog
// carrying our near-white text -- unreadable. This costs nothing when a native
// dialog is used, since Qt ignores the palette for one.
QString MainWindow::askForDirectory(const QString &title) {
  QFileDialog dlg(this, title);
  dlg.setFileMode(QFileDialog::Directory);
  dlg.setOption(QFileDialog::ShowDirsOnly, true);
  applyThemeTo(&dlg);
  return dlg.exec() == QDialog::Accepted ? dlg.selectedFiles().value(0)
                                         : QString();
}

QString MainWindow::askForFile(const QString &title,
                              const QString &filter) {
  QFileDialog dlg(this, title);
  dlg.setFileMode(QFileDialog::ExistingFile);
  dlg.setNameFilter(filter);
  applyThemeTo(&dlg);
  return dlg.exec() == QDialog::Accepted ? dlg.selectedFiles().value(0)
                                         : QString();
}

void MainWindow::browseLibrary() {
  // A search worker iterates index_; the picker below would lead straight to
  // tryLoadIndex() replacing the vector underneath it. The button is gated in
  // updateActions(), but the menu action shares this slot, so the guard has to
  // live here rather than on the widget.
  if (indexing_ || searching_)
    return;

  const QString dir = askForDirectory(QStringLiteral("Select image library"));
  if (dir.isEmpty())
    return;

  libEdit_->setText(dir);
  tryLoadIndex(dir);
}

void MainWindow::tryLoadIndex(const QString &dir) {
  // Same guard as browseLibrary(), at the mutation site: index_.clear() and
  // index_.load() below rewrite the vector a search worker is reading. Cheap
  // defence in depth for any future caller that forgets to check.
  if (indexing_ || searching_)
    return;

  results_->clear();
  // Fall back to the pre-1.1 cache location so existing installs keep
  // working; the next reindex writes to the new path.
  const QString path = core::defaultIndexPath(dir);
  const QString legacy = core::legacyIndexPath(dir);
  const QString load = QFile::exists(path) ? path : legacy;

  if (QFile::exists(load) && index_.load(load) && !index_.empty()) {
    stats_->setText(
        QStringLiteral("Loaded cached index: %1 images (%2 skipped)")
            .arg(index_.size())
            .arg(index_.errorCount()));
    statusBar()->showMessage(QStringLiteral("Loaded cached index"), 4000);
  } else {
    index_.clear();
    stats_->setText(
        QStringLiteral("Library selected — click “Index Library”."));
    statusBar()->showMessage(QStringLiteral("No cached index found"), 4000);
  }
  updateActions();
}

void MainWindow::startIndex() {
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
  indexBtn_->setToolTip(
      QStringLiteral("Stop and discard all data indexed so far"));
  progress_->setRange(0, 100);
  progress_->setValue(0);
  results_->clear();
  setBusy(true);
  statusBar()->showMessage(QStringLiteral("Indexing…"));

  worker_ = std::thread([this, root] {
    // Nothing may escape this thread body. An exception crossing a
    // std::thread calls std::terminate, and OpenCV throws cv::Exception on
    // malformed input -- which is exactly what a whole-filesystem scan of
    // untrusted files hands it. Report the failure instead of vanishing.
    std::unique_ptr<core::ImageIndex> idx;
    bool ok = false;
    QString error;
    try {
      idx = std::make_unique<core::ImageIndex>();
      ok = idx->build(root, [this](const core::BuildProgress &p) {
        QMetaObject::invokeMethod(
            this,
            [this, done = p.done, total = p.total, cur = p.current] {
              onIndexProgress(done, total, cur);
            },
            Qt::QueuedConnection);
        return !cancel_.load();
      });
    } catch (const std::exception &e) {
      ok = false;
      error = QString::fromUtf8(e.what());
    } catch (...) {
      ok = false;
      error = QStringLiteral("unknown error");
    }

    // Handed over by pointer: the entry vector is no longer deep-copied
    // twice (once into the queued event, once for a by-value parameter).
    QMetaObject::invokeMethod(
        this,
        [this, ok, error, idx = std::move(idx)]() mutable {
          onIndexFinished(ok, std::move(idx), error);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::onIndexProgress(int done, int total, const QString &current) {
  if (total <= 0) {
    // Discovery phase: the total file count is not known yet.
    progress_->setRange(0, 0); // busy indicator
    stats_->setText(QStringLiteral("Scanning for images…\n%1").arg(current));
    return;
  }
  progress_->setRange(0, 100);
  // done * 100 in int overflows past ~21.4M files (a whole-disk archive can
  // get close); evaluate in 64 bits, then narrow a value that is in range.
  progress_->setValue(int(qint64(done) * 100 / total));
  stats_->setText(QStringLiteral("Indexing %1/%2\n%3")
                      .arg(done)
                      .arg(total)
                      .arg(QFileInfo(current).fileName()));
}

void MainWindow::onIndexFinished(bool ok,
                                 std::unique_ptr<core::ImageIndex> index,
                                 const QString &error) {
  if (worker_.joinable())
    worker_.join();

  indexBtn_->setText(QStringLiteral("Index Library"));
  indexBtn_->setToolTip(QString());
  indexing_ = false;

  if (!error.isEmpty()) {
    stats_->setText(QStringLiteral("Indexing failed: %1").arg(error));
    statusBar()->showMessage(QStringLiteral("Indexing failed"), 4000);
    setBusy(false);
    return;
  }

  if (!ok || cancel_ || !index) {
    stats_->setText(QStringLiteral("Indexing cancelled."));
    statusBar()->showMessage(QStringLiteral("Indexing cancelled"), 4000);
    setBusy(false);
    return;
  }

  progress_->setValue(100);
  index_ = std::move(*index);

  const bool saved = index_.save(core::defaultIndexPath(libEdit_->text()));

  stats_->setText(QStringLiteral("Indexed %1 images (%2 skipped)%3")
                      .arg(index_.size())
                      .arg(index_.errorCount())
                      .arg(saved ? QStringLiteral(", index saved")
                                 : QStringLiteral(", cache save failed")));
  statusBar()->showMessage(QStringLiteral("Indexing complete"), 4000);
  setBusy(false);
}

void MainWindow::stopIndex() {
  if (!indexing_)
    return;

  cancel_ = true;
  indexBtn_->setText(QStringLiteral("Stopping..."));
  setBusy(true);
  indexBtn_->setEnabled(false); // stay disabled until the worker reports back
  statusBar()->showMessage(
      QStringLiteral("Stopping... partial index will be discarded"), 4000);
}

void MainWindow::browseQuery() {
  // The picker opens a modal native dialog, which is wrong UX while a search
  // or index is running, and replacing the query mid-search is at best
  // confusing. Gated here as well as in updateActions(), the same way as
  // browseLibrary(), so both File menu actions are covered either way.
  if (indexing_ || searching_)
    return;

  static const QString filter = [] {
    QSet<QString> exts;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
      exts.insert(QString::fromLatin1(f).toLower());
    exts << QStringLiteral("png") << QStringLiteral("jpg")
         << QStringLiteral("jpeg") << QStringLiteral("bmp")
         << QStringLiteral("gif");

    QStringList list(exts.cbegin(), exts.cend());
    list.sort();

    QString patterns;
    for (const QString &e : list)
      patterns += QStringLiteral(" *.") + e;

    return QStringLiteral("Images (%1);;All files (*)").arg(patterns);
  }();

  const QString file =
      askForFile(QStringLiteral("Select query image"), filter);
  if (file.isEmpty())
    return;

  queryEdit_->setText(file);
  showPreview(file);
  updateActions();
}

void MainWindow::showPreview(const QString &path) {
  const QPixmap pm(path);
  if (pm.isNull()) {
    preview_->setText(QStringLiteral("cannot load"));
    return;
  }
  preview_->setPixmap(pm.scaled(preview_->size(), Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
}

void MainWindow::startSearch() {
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
    // Same rule as the index worker: an escaping exception would abort the
    // process, and this path decodes images from arbitrary paths.
    std::vector<core::SearchResult> results;
    bool ok = false;
    QString error;
    try {
      ok = index_.searchFile(
          query, threshold, results, [this](int done, int total) {
            QMetaObject::invokeMethod(
                this, [this, done, total] { onSearchProgress(done, total); },
                Qt::QueuedConnection);
            return !cancel_.load();
          });
    } catch (const std::exception &e) {
      ok = false;
      results.clear();
      error = QString::fromUtf8(e.what());
    } catch (...) {
      ok = false;
      results.clear();
      error = QStringLiteral("unknown error");
    }

    QMetaObject::invokeMethod(
        this,
        [this, ok, error, results = std::move(results)]() mutable {
          onSearchFinished(ok, std::move(results), error);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::stopSearch() {
  if (!searching_)
    return;

  cancel_ = true;
  searchBtn_->setText(QStringLiteral("Stopping..."));
  searchBtn_->setEnabled(false);
  statusBar()->showMessage(QStringLiteral("Stopping..."), 4000);
}

void MainWindow::onSearchProgress(int done, int total) {
  if (total > 0) {
    // Same 64-bit guard as onIndexProgress; this is also called from stage one
    // of the search now (the in-memory prefilter over the whole index), so the
    // label must not claim a comparison is running when it is only ranking.
    progress_->setValue(int(qint64(done) * 100 / total));
    stats_->setText(QStringLiteral("Searching %1/%2").arg(done).arg(total));
  }
}

void MainWindow::onSearchFinished(bool ok,
                                  std::vector<core::SearchResult> results,
                                  const QString &error) {
  if (searchWorker_.joinable())
    searchWorker_.join();

  searching_ = false;
  searchBtn_->setText(QStringLiteral("Search"));
  searchBtn_->setToolTip(QString());
  setBusy(false);

  if (!error.isEmpty()) {
    stats_->setText(QStringLiteral("Search failed: %1").arg(error));
    statusBar()->showMessage(QStringLiteral("Search failed"), 4000);
    return;
  }
  if (!ok || cancel_) {
    stats_->setText(QStringLiteral("Search cancelled."));
    statusBar()->showMessage(QStringLiteral("Search cancelled"), 4000);
    return;
  }
  if (results.empty()) {
    stats_->setText(QStringLiteral("No matches above threshold."));
    statusBar()->showMessage(QStringLiteral("No matches"), 4000);
    return;
  }

  const qint64 ms = searchTimer_.isValid() ? searchTimer_.elapsed() : 0;
  renderResults(results);
  statusBar()->showMessage(QStringLiteral("Found %1 results in %2 ms")
                               .arg(results_->count())
                               .arg(ms),
                           5000);
}

void MainWindow::renderResults(const std::vector<core::SearchResult> &results) {
  results_->clear();
  for (const auto &r : results) {
    // core::search already applied the threshold, and it is the only place
    // that decides what a match is. Re-filtering here used the live widget
    // value, which made the GUI a second source of truth that could disagree
    // with the CLI.
    const int pct = int(r.score * 100.0 + 0.5);

    // Decode straight to thumbnail size rather than loading the full
    // image, which matters when results can be very large photographs.
    QImageReader reader(r.absPath);
    const QSize native = reader.size();
    if (native.isValid() && !native.isEmpty())
      reader.setScaledSize(native.scaled(QSize(128, 128), Qt::KeepAspectRatio));
    const QImage thumb = reader.read();

    auto *item = new QListWidgetItem(
        QIcon(QPixmap::fromImage(thumb)),
        QStringLiteral("%1%2\n%3")
            .arg(r.exact ? QStringLiteral("EXACT ") : QString())
            .arg(pct)
            .arg(QFileInfo(r.absPath).fileName()));
    item->setData(Qt::UserRole, r.absPath);
    item->setToolTip(
        r.absPath + QStringLiteral("\nscore: ") +
        QString::number(r.score, 'f', 3) +
        (r.exact ? QStringLiteral("\nbyte-identical copy") : QString()));
    item->setSizeHint({170, 200});
    results_->addItem(item);
  }
}

void MainWindow::setBusy(bool busy) {
  // While a worker holds index_ or the result set, the inputs that would
  // invalidate them are locked out.
  browseLibBtn_->setEnabled(!busy);
  browseQueryBtn_->setEnabled(!busy);
  thresholdSpin_->setEnabled(!busy);
  updateActions();
}

void MainWindow::openResult(QListWidgetItem *item) {
  const QString path = item->data(Qt::UserRole).toString();
  if (!path.isEmpty())
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::updateActions() {
  const bool busy = indexing_ || searching_;
  const bool canIndex = !busy && !libEdit_->text().isEmpty();
  indexBtn_->setEnabled(indexing_ || canIndex);
  browseLibBtn_->setEnabled(!busy);
  browseQueryBtn_->setEnabled(!busy);
  searchBtn_->setEnabled(
      busy ? searching_ : (!index_.empty() && !queryEdit_->text().isEmpty()));
  // The File menu mirrors the browse buttons. These actions replace index_
  // via browseLibrary(), so they must be locked out for exactly the same
  // duration as the button, or a search worker reads freed memory.
  if (indexAction_)
    indexAction_->setEnabled(!busy);
  if (queryAction_)
    queryAction_->setEnabled(!busy);
}

// ----- theme ---------------------------------------------------------------

bool MainWindow::systemPrefersDark() const {
  // QStyleHints::colorScheme() is the correct answer, but it only exists from
  // Qt 6.5. The luminance check below works on every Qt 6 and agrees with it on
  // the platforms that report a scheme at all, so the version guard is belt and
  // braces rather than the primary mechanism.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
  if (scheme == Qt::ColorScheme::Dark)
    return true;
  if (scheme == Qt::ColorScheme::Light)
    return false;
  // Unknown, or a Qt too old to answer, falls through to the luminance check.
#endif
  return relativeLuminance(desktopPalette_.color(QPalette::Window)) <
         relativeLuminance(desktopPalette_.color(QPalette::WindowText));
}

void MainWindow::setDark(bool dark) {
  if (darkMode_ == dark)
    return;
  darkMode_ = dark;
  applyTheme();
}

void MainWindow::toggleTheme() { setDark(!darkMode_); }

void MainWindow::updateThemeGlyph() {
  themeToggleBtn_->setIcon(
      paintThemeGlyph(darkMode_, palette().color(QPalette::Link),
                      themeToggleBtn_->iconSize().width()));
  // The glyph shows the current theme; the tooltip has to name the other one,
  // because an icon on its own gives no hint of what clicking will do.
  themeToggleBtn_->setToolTip(darkMode_
                                  ? QStringLiteral("Switch to light mode")
                                  : QStringLiteral("Switch to dark mode"));
}

void MainWindow::equalizePanelHeights() {
  if (!libGroup_ || !queryGroup_ || panelsSized_)
    return;

  // Measured exactly once, before anything is pinned. A QGroupBox's size hint
  // is floored by its own minimum height, so reading it back after pinning
  // would be a ratchet: the pair could only ever grow, and the taller panel
  // would drag the shorter one up on every re-measure.
  //
  // Fixed heights rather than minimums, because a fixed height also raises
  // the window's minimum size hint. The two panels therefore cannot be
  // squeezed to different heights by a short window -- the window simply
  // refuses to become shorter than they are.
  const int tallest = std::max(libGroup_->sizeHint().height(),
                               queryGroup_->sizeHint().height());
  libGroup_->setFixedHeight(tallest);
  queryGroup_->setFixedHeight(tallest);
  panelsSized_ = true;
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);
  // Deferred to here rather than the constructor: sizeHint() is only
  // trustworthy once the widget has its final font metrics, which arrive
  // after polish.
  equalizePanelHeights();

  // The update check goes out from here rather than the constructor because it
  // is a network round trip, and the window must not wait on it to appear. The
  // delay is not for the network's sake: it lets the window paint first, so an
  // available update raises its box over a finished window instead of a white
  // flash. The checker applies its own opt-out and daily throttle, so this
  // fires on every launch and costs nothing on all but one.
  QTimer::singleShot(500, this,
                     [this] { updates_->checkOnStartup(); });
}

void MainWindow::applyThemeTo(QWidget *target) const {
  // Every colour below comes out of the palette the desktop reports. When the
  // toggle agrees with the desktop, that palette is used exactly as given. When
  // it disagrees -- someone has overridden the theme by hand -- the opposite
  // scheme is built from it, because Qt 6.4 can neither report nor hold a
  // palette for the scheme the desktop is not currently using.
  // desktopPalette_, not QGuiApplication::palette(): the latter has by now been
  // overwritten with the derived theme, so reading it would make this a
  // derivation of the previous derivation.
  const bool osIsDark =
      relativeLuminance(desktopPalette_.color(QPalette::Window)) <
      relativeLuminance(desktopPalette_.color(QPalette::WindowText));
  const ThemeColours c = coloursFromPalette(
      osIsDark == darkMode_ ? desktopPalette_ : invertedPalette(desktopPalette_));

  QString qss = QString::fromLatin1(R"CSS(
QWidget { color: @text; font-size: 13px; }
QMainWindow, QWidget#centralWidget { background: @window; }

QLabel#titleLabel {
    font-size: 15px; font-weight: 600; color: @text;
    padding: 2px 2px 6px 2px;
}
QLabel#statsLabel { color: @muted; }
QLabel#previewLabel {
    border: 1px dashed @border; border-radius: 6px;
    color: @muted; background: @field;
}

QGroupBox {
    background: @panel; border: 1px solid @border; border-radius: 8px;
    margin-top: 14px; padding: 12px 10px 10px 10px;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    left: 10px; padding: 0 4px; color: @muted; font-weight: 600;
}

QLineEdit, QSpinBox {
    background: @field; border: 1px solid @border; border-radius: 6px;
    padding: 5px 8px; selection-background-color: @accent;
}
QLineEdit:read-only { color: @muted; }
QLineEdit:focus, QSpinBox:focus { border-color: @accent; }
/* The arrows are left to Qt to draw from the palette rather than replaced with
   images, so they stay legible in both themes. */
QSpinBox { padding-right: 20px; }
QSpinBox::up-button, QSpinBox::down-button {
    subcontrol-origin: border; width: 18px; background: transparent;
    border: none; border-left: 1px solid @border;
}
QSpinBox::up-button { subcontrol-position: top right; }
QSpinBox::down-button { subcontrol-position: bottom right; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; }

QPushButton {
    background: @field; border: 1px solid @border; border-radius: 6px;
    padding: 6px 14px; color: @text;
}
QPushButton:hover:enabled { background: @hover; }
QPushButton:pressed:enabled { background: @pressed; }
QPushButton:disabled { color: @muted; border-color: @border; }

#searchBtn, #indexBtn {
    background: @accent; color: @onAccent; border: 1px solid @accent;
    font-weight: 600; padding: 7px 14px;
}
#searchBtn:hover:enabled, #indexBtn:hover:enabled {
    background: @accentHover; color: @onAccentHover;
}
#searchBtn:pressed:enabled, #indexBtn:pressed:enabled {
    background: @accentPressed; color: @onAccentPressed;
}
#searchBtn:disabled, #indexBtn:disabled {
    background: @disabled; color: @muted; border-color: @border;
}

/* The general button rule pads 6px/14px for a text label. This one holds only
   an icon at a fixed square size, so the padding has to go or it squeezes the
   glyph and inflates the widget. */
#themeToggleBtn { padding: 0; }

QProgressBar {
    background: @field; border: 1px solid @border; border-radius: 6px;
    text-align: center; height: 16px; color: @text;
}
QProgressBar::chunk { background: @accent; border-radius: 5px; }

QListWidget#resultsView {
    background: @panel; border: 1px solid @border; border-radius: 8px;
    padding: 6px; outline: none;
    margin-top: 14px;
}
QListWidget#resultsView::item { border-radius: 6px; color: @text; }
QListWidget#resultsView::item:selected { background: @checked; color: @onChecked; }
QListWidget#resultsView::item:hover { background: @itemHover; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical {
    background: @border; border-radius: 5px; min-height: 28px;
}
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal {
    background: @border; border-radius: 5px; min-width: 28px;
}
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

/* A popup menu is a top-level widget with its own surface, so it is given the
   panel colour explicitly rather than inheriting the window's. */
QMenuBar {
    background: @panel; color: @text; border-bottom: 1px solid @border;
}
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: @itemHover; }
QMenuBar::item:disabled { color: @muted; }
QMenu {
    background: @panel; color: @text; border: 1px solid @border; padding: 4px;
}
QMenu::item { padding: 5px 24px 5px 22px; border-radius: 4px; }
QMenu::item:selected { background: @checked; color: @onChecked; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QStatusBar { background: @panel; color: @muted; border-top: 1px solid @border; }
QStatusBar::item { border: none; }

QToolTip {
    background: @panel; color: @text;
    border: 1px solid @border; border-radius: 4px; padding: 4px 6px;
}
)CSS");

  // One substitution table keeps light and dark differing only by colour, so
  // the layout above cannot drift out of sync between the two themes.
  // Longest token first: @accentHover begins with @accent, so replacing
  // @accent before @accentHover would leave "#308cc6Hover" in the sheet, which
  // Qt parses as an unknown colour and drops silently.
  QVector<QPair<QString, QColor>> vars = {
      {QStringLiteral("@window"), c.window},
      {QStringLiteral("@panel"), c.panel},
      {QStringLiteral("@field"), c.field},
      {QStringLiteral("@text"), c.text},
      {QStringLiteral("@muted"), c.muted},
      {QStringLiteral("@border"), c.border},
      {QStringLiteral("@accent"), c.accent},
      {QStringLiteral("@onAccent"), c.onAccent},
      {QStringLiteral("@onAccentHover"), c.onAccentHover},
      {QStringLiteral("@onAccentPressed"), c.onAccentPressed},
      {QStringLiteral("@accentHover"), c.accentHover},
      {QStringLiteral("@accentPressed"), c.accentPressed},
      {QStringLiteral("@disabled"), c.disabled},
      {QStringLiteral("@hover"), c.hover},
      {QStringLiteral("@pressed"), c.pressed},
      {QStringLiteral("@itemHover"), c.itemHover},
      {QStringLiteral("@checked"), c.checked},
      {QStringLiteral("@onChecked"), c.onChecked},
  };
  std::stable_sort(vars.begin(), vars.end(),
                   [](const QPair<QString, QColor> &a,
                      const QPair<QString, QColor> &b) {
                     return a.first.size() > b.first.size();
                   });
  for (const auto &v : vars)
    qss.replace(v.first, v.second.name(QColor::HexRgb));

  target->setStyleSheet(qss);

  // The stylesheet covers the widgets it names, but parts Qt draws natively
  // from the palette -- spin box arrows, and anything in a dialog -- are
  // unaffected by it and would come out dark-on-dark. Setting the palette to
  // match closes that gap, and covers the file and message dialogs too,
  // which inherit this palette as children of the window.
  QPalette pal;
  pal.setColor(QPalette::Window, c.window);
  pal.setColor(QPalette::WindowText, c.text);
  pal.setColor(QPalette::Base, c.field);
  pal.setColor(QPalette::AlternateBase, c.panel);
  pal.setColor(QPalette::Text, c.text);
  pal.setColor(QPalette::Button, c.panel);
  pal.setColor(QPalette::ButtonText, c.text);
  pal.setColor(QPalette::BrightText, Qt::red);
  pal.setColor(QPalette::Highlight, c.accent);
  pal.setColor(QPalette::HighlightedText, c.onAccent);
  pal.setColor(QPalette::ToolTipBase, c.panel);
  pal.setColor(QPalette::ToolTipText, c.text);
  pal.setColor(QPalette::PlaceholderText, c.muted);
  pal.setColor(QPalette::Link, c.accent);
  pal.setColor(QPalette::Mid, c.border);
  pal.setColor(QPalette::Dark, c.border.darker(140));
  pal.setColor(QPalette::Shadow, Qt::black);
  // Disabled text is the muted colour, so a greyed control still reads as
  // disabled rather than as a differently-coloured enabled one.
  pal.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);
  pal.setColor(QPalette::Disabled, QPalette::Text, c.muted);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
  target->setPalette(pal);
}

void MainWindow::applyTheme() {
  applyThemeTo(this);

  // A palette set on a widget propagates only to that widget's children. A
  // dialog is a top-level window with no parent, so it keeps the platform
  // default palette -- a white file dialog -- while still picking up our
  // stylesheet's near-white text, which is how the popup ended up white on
  // white. Setting it application-wide covers every dialog, including the
  // message boxes, which have the same problem.
  qApp->setPalette(palette());

  updateThemeGlyph();
}
