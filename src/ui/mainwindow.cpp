#include "ui/mainwindow.h"

#include "ui/file_actions.h"
#include "ui/model_download.h"

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileIconProvider>
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
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPalette>
#include <QPointer>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QKeyEvent>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

QString imageOpenFilter()
{
  QSet<QString> exts;
  for (const QByteArray &f : QImageReader::supportedImageFormats())
    exts.insert(QString::fromLatin1(f).toLower());
  exts << QStringLiteral("png") << QStringLiteral("jpg")
       << QStringLiteral("jpeg") << QStringLiteral("bmp")
       << QStringLiteral("webp");
  const QStringList sorted = exts.values();
  QStringList patterns;
  for (const QString &e : sorted)
    patterns << QStringLiteral("*.%1").arg(e);
  return QStringLiteral("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}

QImage checkerboardUnder(const QImage &cutout, int cell = 10)
{
  const QImage base(cutout.width(), cutout.height(), QImage::Format_RGB32);
  const auto shade = [&](int x, int y) {
    return ((x / cell) + (y / cell)) % 2 == 0 ? qRgb(190, 190, 190)
                                              : qRgb(150, 150, 150);
  };
  for (int y = 0; y < base.height(); ++y) {
    QRgb *line = reinterpret_cast<QRgb *>(
        const_cast<uchar *>(base.constScanLine(y)));
    for (int x = 0; x < base.width(); ++x)
      line[x] = shade(x, y);
  }
  QImage out = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter p(&out);
  p.drawImage(0, 0, cutout.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  p.end();
  return out;
}

const char *kPrivacyPolicyText = R"PRIVACY(
LucidGrasp Privacy Policy

Last reviewed: October 2026

LucidGrasp is designed so that your work stays on your machine. Your library,
your searches and your settings are never collected, uploaded or sold. This
policy states exactly what the application does with your data.

DATA CONTROLLER
The data controller is the project maintainer, reachable at
adityaraj1234@duck.com for all privacy requests. The project is a free,
open-source effort that processes no personal data on its own behalf.

DATA THAT STAYS ON YOUR COMPUTER

Indexing and matching read image files only from folders you choose. Every
similarity comparison is computed on your own machine and no image data ever
leaves it.

The search index is cached to your own disk in your system's standard
per-user data directory (for example ~/.local/share/LucidGrasp/indexes/ on
Linux). You can delete the cache at any time to force a rebuild; nothing is
uploaded anywhere.

Preferences such as whether automatic update checks are enabled and which
release version you are currently ignoring are stored in a per-user INI file
in your system's standard configuration directory.

THE ONE TIME DATA LEAVES YOUR COMPUTER

Update checks. On launch, unless you have turned automatic checks off in the
Updates menu, LucidGrasp makes a single HTTPS request to the GitHub API asking
for the latest release announcement of this project. The request carries only
the repository identifier and the version number already installed on your
machine. It does not include your name, your files or your library contents.
As with any website visit, GitHub receives your IP address and the ordinary
HTTP headers your browser or this application sends; GitHub Inc. is based in
the United States, and GitHub's own privacy policy applies to that one
request. This is necessary for the legitimate interest of notifying users of
security fixes and new releases, and it can be switched off entirely in the
Updates menu. If a newer release exists you are shown a dialog with a link;
nothing is downloaded or installed unless you choose to do it yourself.

Help menu links. "Report an Issue", "Request a Feature", "View on GitHub" and
"Contact the Developer" open whichever external service they name (GitHub or
your mail application) using your normal default apps. Those services have
their own privacy policies.

WHAT LUCIDGRASP NEVER DOES

No analytics, no telemetry, no crash reporting, no advertising, no cookies,
no accounts and no background services that phone home. There is no tracking
of any kind, and no sale or sharing of personal data: the "sale" and
"sharing" definitions under the CCPA/CPRA and comparable US state privacy
laws never apply, so there is nothing to opt out of.

LucidGrasp does not inspect, upload or transmit your images, folders or search
queries to anyone, and no user data is used to train any machine-learning
model.

CHILDREN
The Software is not directed at children. Consistent with COPPA (US) and the
child-consent age set by EU member states (13 to 16 under the GDPR),
LucidGrasp does not knowingly collect personal information from anyone under
13.

YOUR RIGHTS
LucidGrasp holds no personal data about you, but the rights below are honored
to the fullest extent the law requires. Write to adityaraj1234@duck.com to
exercise any of them: access and know, correct, delete, restrict, object,
portability, no discrimination for exercising a right, and submission by a
duly authorized agent. Requests are answered within the statutory deadlines
(30 days under GDPR/UK GDPR; 45 days under CCPA/CPRA). Under GDPR Article 77
you may also complain to your local supervisory authority.

CONTACT

Questions about this policy: adityaraj1234@duck.com.
)PRIVACY";

const char *kTermsText = R"TERMS(
LucidGrasp Terms of Use

Effective: October 2026

These terms govern your use of the LucidGrasp application, its source code,
and its releases. By downloading, installing, using, or redistributing the
Software you agree to them. If you do not agree, do not install or use it.

THE SOFTWARE IS FREE

LucidGrasp is distributed free of charge. Nothing in it is sold, licensed for
a fee, or offered as a subscription. Because nothing is purchased there is no
price, no cancellation process, and no refund policy is needed or offered.
Any copy of LucidGrasp offered for money by a third party is not authorized
by this project.

OPEN SOURCE LICENSE

The source code is licensed under the MIT License (see the LICENSE file),
which permits use, copying, modification, merging, publication,
distribution, sublicensing, and sale of the source code subject to its
conditions.

UPDATES

Unless automatic checks are turned off, the Software checks GitHub on launch
for a newer release announcement and shows a dialog when one exists (see the
Privacy Policy). Nothing is downloaded or installed automatically.

ACCEPTABLE USE AND LIABILITY

You agree to use the Software lawfully and only on systems you own or are
authorized to access, as described in the Acceptable Use Policy. The Software
is provided "as is" without warranty of any kind, and the authors and
copyright holders are not liable for any damages arising from its use, as
described in DISCLAIMER.md.

THIRD-PARTY SERVICES

The update check talks to the GitHub API; Help menu links use your default
browser or mail application. Those services' own terms and policies apply to
those interactions.

INTERNATIONAL USE AND EXPORT

The Software is open source and publicly available. Under the US Export
Administration Regulations, publicly available open source software is
generally not subject to the EAR. You remain responsible for complying with
the laws of the country in which you use or redistribute it.

GOVERNING LAW

These terms are governed by the laws of the Republic of India, without regard
to its conflict-of-law rules, and disputes are subject to the exclusive
jurisdiction of the courts of India.

CHANGES AND CONTACT

These terms may be revised in this repository as the project evolves.
Questions: adityaraj1234@duck.com.
)TERMS";

const char *kDisclaimerText = R"DISCLAIMER(
LucidGrasp Disclaimer of Warranties and Limitation of Liability

Effective: October 2026

AS-IS DISCLAIMER
THE SOFTWARE IS PROVIDED "AS IS" AND "AS AVAILABLE", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, AND
NON-INFRINGEMENT.

LucidGrasp is developed and published by an individual maintainer on a
best-effort basis. It is not warranted that the Software will be
uninterrupted, error-free, secure, virus-free, or that defects will be
corrected.

NO GUARANTEE OF RESULTS
Image similarity scoring is heuristic. No particular search result, ranking,
or accuracy percentage is guaranteed, and results are for informational use
only. They must not be relied upon for safety-critical, medical, or
regulatory decisions.

LIMITATION OF LIABILITY
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE AUTHORS AND COPYRIGHT
HOLDERS SHALL NOT BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF,
OR IN CONNECTION WITH THE SOFTWARE, ITS USE, OR ITS RESULTS - INCLUDING
INDIRECT, INCIDENTAL, CONSEQUENTIAL, OR EXEMPLARY DAMAGES, LOSS OF DATA, LOSS
OF PROFITS, OR BUSINESS INTERRUPTION.

The software is free; nothing is sold; these limitations are a fundamental
basis of its availability.

NOT ADVICE
Nothing in the Software, its documentation, or its output constitutes legal,
medical, security, or regulatory advice, a compliance review, or a
certification of compliance with any law or framework.
)DISCLAIMER";

const char *kAcceptableUseText = R"USE(
LucidGrasp Acceptable Use Policy

Effective: October 2026

LucidGrasp indexes folders on your own machine and finds visually similar
images. You must use it within these limits.

LAWFUL USE ONLY
Use the Software only in compliance with applicable law, including copyright
and other intellectual property law, data protection and privacy law,
computer misuse and unauthorized-access law, and export, import, and
sanctions law that applies to you.

AUTHORIZED SYSTEMS ONLY
Index only files and systems you own or are authorized by the owner to
access. Do not use the Software against systems, networks, or data you have
no right to read.

WHAT YOU MAY NOT DO
You may not use the Software to plan, facilitate, or commit any unlawful act;
to infringe another person's rights; to surveil, track, or profile
individuals without lawful basis; to bypass security or access controls you
are not authorized to use; to deliver malware; or to misrepresent the
Software, its author, or its affiliation.

REDISTRIBUTION
The source code and binaries may be redistributed under the MIT License.
Redistributors must keep the license text and copyright notice, mark their
modifications clearly, and must not represent modified copies as the
official project.

ENFORCEMENT
Report violations and vulnerabilities privately: security matters go through
SECURITY.md (GitHub private vulnerability reporting); everything else goes to
adityaraj1234@duck.com. This policy supplements, and does not replace, the
Terms of Use and the Disclaimer.
)USE";

bool isHeadingLine(const QString &line) {
  if (line.isEmpty() || line.size() > 44)
    return false;
  bool hasLetter = false;
  for (const QChar c : line) {
    if (c.isLetter()) {
      if (!c.isUpper())
        return false;
      hasLetter = true;
    } else if (!c.isSpace() && !c.isDigit() && c != QLatin1Char('-') &&
               c != QLatin1Char('.')) {
      return false;
    }
  }
  return hasLetter;
}

QString reflowLegalText(const QString &text) {
  QStringList paragraphs;
  QString paragraph;
  const QStringList lines = text.split(QLatin1Char('\n'));
  for (const QString &sourceLine : lines) {
    const QString line = sourceLine.trimmed();
    if (line.isEmpty()) {
      if (!paragraph.isEmpty()) {
        paragraphs.append(paragraph);
        paragraph.clear();
      }
      continue;
    }
    if (!paragraph.isEmpty())
      paragraph += QLatin1Char(' ');
    paragraph += line;
    if (isHeadingLine(line)) {
      paragraphs.append(paragraph);
      paragraph.clear();
    }
  }
  if (!paragraph.isEmpty())
    paragraphs.append(paragraph);
  return paragraphs.join(QStringLiteral("\n\n"));
}

class AgreementDialog : public QDialog {
public:
  using QDialog::QDialog;

protected:
  void reject() override {}
  void closeEvent(QCloseEvent *event) override {
    event->accept();
    QCoreApplication::quit();
  }
};

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
    const qreal ox = s * 0.11, oy = s * 0.04, d = s * 0.92;
    QPainterPath disc, bite;
    disc.addEllipse(QRectF(ox, oy, d, d));
    bite.addEllipse(QRectF(ox + d * 0.28, oy - d * 0.14, d, d));
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(disc.subtracted(bite));
  } else {
    const QRectF core(s * 0.32, s * 0.32, s * 0.36, s * 0.36);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawEllipse(core);

    const QPointF mid = core.center();
    const qreal inner = s * 0.32, outer = s * 0.46;
    p.setPen(QPen(colour, qMax(1.0, s * 0.06), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 8; ++i) {
      const qreal a = i * kPi / 4.0;
      const qreal dx = std::cos(a), dy = std::sin(a);
      p.drawLine(QPointF(mid.x() + dx * inner, mid.y() + dy * inner),
                 QPointF(mid.x() + dx * outer, mid.y() + dy * outer));
    }
  }
  p.end();
  return QIcon(pixmap);
}

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
  QColor window;
  QColor panel;
  QColor field;
  QColor text;
  QColor muted;
  QColor border;
  QColor accent;
  QColor onAccent;
  QColor onAccentHover;
  QColor onAccentPressed;
  QColor accentHover;
  QColor accentPressed;
  QColor disabled;
  QColor hover;
  QColor pressed;
  QColor itemHover;
  QColor checked;
  QColor onChecked;
};

ThemeColours coloursFromPalette(const QPalette &p) {
  const QColor text = p.color(QPalette::WindowText);
  const QColor accent = p.color(QPalette::Highlight);
  QColor window = p.color(QPalette::Window);
  const QColor base = p.color(QPalette::Base);

  if (contrastRatio(base, window) < 1.02)
    window = mixToward(window, text, 0.05);

  const QColor panel = base, field = base;

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

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), desktopPalette_(QGuiApplication::palette()) {
  setWindowTitle(QStringLiteral("LucidGrasp"));

  auto *central = new QWidget(this);
  central->setObjectName(QStringLiteral("centralWidget"));
  auto *rootLayout = new QVBoxLayout(central);

  auto *topBar = new QHBoxLayout;
  topBar->setContentsMargins(0, 0, 0, 0);
  topBar->setSpacing(8);

  auto *title = new QLabel(QStringLiteral("LucidGrasp"), central);
  title->setObjectName(QStringLiteral("titleLabel"));
  topBar->addWidget(title);
  topBar->addStretch(1);

  const int side = fontMetrics().height() + 12;
  const auto makeToolButton = [&](const QString &objectName,
                                  const QString &tip) -> QPushButton * {
    auto *btn = new QPushButton(central);
    btn->setObjectName(objectName);
    btn->setToolTip(tip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(side - 12, side - 12));
    btn->setFixedSize(side, side);
    return btn;
  };

  themeToggleBtn_ =
      makeToolButton(QStringLiteral("themeToggleBtn"), QString());
  topBar->addWidget(themeToggleBtn_);

  revealBtn_ = makeToolButton(QStringLiteral("revealBtn"),
                              QStringLiteral("Show in file manager"));
  topBar->addWidget(revealBtn_);

  trashBtn_ = makeToolButton(QStringLiteral("trashBtn"),
                             QStringLiteral("Move to trash"));
  topBar->addWidget(trashBtn_);

  rootLayout->addLayout(topBar);

  auto *bodyLayout = new QHBoxLayout;

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
  connect(revealBtn_, &QPushButton::clicked, this, &MainWindow::revealResult);
  connect(trashBtn_, &QPushButton::clicked, this, &MainWindow::trashResult);
  connect(results_, &QListWidget::currentItemChanged, this,
           [this](QListWidgetItem *) { updateResultActions(); });
  connect(themeToggleBtn_, &QPushButton::clicked, this,
           &MainWindow::toggleTheme);

  results_->installEventFilter(this);

  updates_ = new app::UpdateChecker(this);
  connect(updates_, &app::UpdateChecker::updateAvailable, this,
          [this](const QString &tag, const QString &url,
                 const QString &notes) { showUpdateDialog(tag, url, notes); });
  connect(updates_, &app::UpdateChecker::finished, this,
          &MainWindow::onUpdateCheckFinished);

  buildMenus();

  darkMode_ = systemPrefersDark();
  applyTheme();
  updateActions();

  QTimer::singleShot(0, this, &MainWindow::showWelcomeDialog);
}

MainWindow::~MainWindow() {
  cancel_ = true;
  if (worker_.joinable())
    worker_.join();
  if (searchWorker_.joinable())
    searchWorker_.join();
  if (bgWorker_.joinable())
    bgWorker_.join();
}

void MainWindow::buildMenus() {
  QMenuBar *bar = menuBar();

  QMenu *file = bar->addMenu(tr("&File"));

  indexAction_ = file->addAction(tr("Select &Library..."));
  indexAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
  connect(indexAction_, &QAction::triggered, this, &MainWindow::browseLibrary);

  queryAction_ = file->addAction(tr("Select &Query Image..."));
  queryAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Q")));
  connect(queryAction_, &QAction::triggered, this, &MainWindow::browseQuery);

  file->addSeparator();
  QAction *quitAction = file->addAction(tr("E&xit"));
  quitAction->setShortcut(QKeySequence::Quit);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  QMenu *updatesMenu = bar->addMenu(tr("&Updates"));

  QAction *checkAction = updatesMenu->addAction(tr("&Check for Updates..."));
  checkAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
  connect(checkAction, &QAction::triggered, this, [this] {
    updateCheckManual_ = true;
    updates_->checkNow();
  });

  autoUpdateAction_ =
      updatesMenu->addAction(tr("Check for Updates &Automatically"));
  autoUpdateAction_->setCheckable(true);
  autoUpdateAction_->setChecked(!updates_->isDisabled());
  connect(autoUpdateAction_, &QAction::toggled, this, [this](bool on) {
    updates_->setDisabled(!on);
    if (on) {
      updateCheckManual_ = true;
      updates_->checkNow();
    }
  });

  updatesMenu->addSeparator();
  QAction *notesAction = updatesMenu->addAction(tr("Latest &Release Notes"));
  connect(notesAction, &QAction::triggered, this, [this] {
    openExternal(QStringLiteral("https://github.com/" LUCIDGRASP_REPO
                                "/releases/latest"));
  });

  QMenu *tools = bar->addMenu(tr("&Tools"));

  bgAction_ = tools->addAction(tr("Remove &Background…"));
  bgAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
  connect(bgAction_, &QAction::triggered, this,
          &MainWindow::startBackgroundRemoval);

  QMenu *help = bar->addMenu(tr("&Help"));

  QAction *reportAction = help->addAction(tr("&Report an Issue..."));
  connect(reportAction, &QAction::triggered, this, [this] {
    openIssuePage(
        QStringLiteral("Bug: "),
        QStringLiteral(
            "**What happened?**\n\n"
            "\n\n"
            "**What did you expect to happen instead?**\n\n"
            "\n\n"
            "**Steps to reproduce**\n\n"
            "1. \n2. \n\n"
            "**LucidGrasp version**\n%1\n\n"
            "**Operating system**\n(e.g. Windows 11, Ubuntu 24.04, macOS 14)\n")
            .arg(QStringLiteral(LUCIDGRASP_VERSION)));
  });

  QAction *featureAction = help->addAction(tr("&Request a Feature..."));
  connect(featureAction, &QAction::triggered, this, [this] {
    openIssuePage(
        QStringLiteral("Feature request: "),
        QStringLiteral(
            "**What problem are you trying to solve?**\n\n"
            "\n\n"
            "**Describe the feature you would like**\n\n"
            "\n\n"
            "**Alternatives you considered**\n\n"
            "\n\n"
            "**Anything else?**\n(e.g. mockups, example images, links)\n"));
  });

  help->addSeparator();

  QAction *contactAction = help->addAction(tr("&Contact the Developer"));
  connect(contactAction, &QAction::triggered, this, [this] {
    QUrl url(QStringLiteral("mailto:adityaraj1234@duck.com"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("subject"), tr("LucidGrasp enquiry"));
    url.setQuery(query);
    QDesktopServices::openUrl(url);
  });

  QAction *repoAction = help->addAction(tr("View on &GitHub"));
  connect(repoAction, &QAction::triggered, this, [this] {
    openExternal(QStringLiteral("https://github.com/" LUCIDGRASP_REPO));
  });

  help->addSeparator();

  QAction *privacyAction = help->addAction(tr("&Privacy Policy..."));
  connect(privacyAction, &QAction::triggered, this,
          &MainWindow::showPrivacyDialog);

  QAction *legalAction = help->addAction(tr("Legal &Notices..."));
  connect(legalAction, &QAction::triggered, this,
          &MainWindow::showLegalNoticesDialog);

  help->addSeparator();

  QAction *aboutAction = help->addAction(tr("&About LucidGrasp"));
  connect(aboutAction, &QAction::triggered, this,
          &MainWindow::showAboutDialog);
}

void MainWindow::openExternal(const QString &url) {
  if (!QDesktopServices::openUrl(QUrl(url)))
    statusBar()->showMessage(tr("Could not open %1").arg(url), 6000);
}

void MainWindow::openIssuePage(const QString &title, const QString &body) {
  QUrl url(QStringLiteral("https://github.com/" LUCIDGRASP_REPO
                          "/issues/new"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("title"), title);
  query.addQueryItem(QStringLiteral("body"), body);
  url.setQuery(query);
  QDesktopServices::openUrl(url);
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(
      this, tr("About LucidGrasp"),
      tr("<b>LucidGrasp %1</b><br><br>"
         "Local image search. Index a folder, hand it an image, and it finds "
         "every visually similar file — all on your own machine.<br><br>"
         "Project: %2<br>"
         "Contact: %3<br><br>"
         "Licensed under the MIT License.")
          .arg(QStringLiteral(LUCIDGRASP_VERSION),
               QStringLiteral("https://github.com/" LUCIDGRASP_REPO),
               QStringLiteral("adityaraj1234@duck.com")));
}

void MainWindow::showWelcomeDialog() {
  QSettings agreement(QSettings::IniFormat, QSettings::UserScope,
                      QCoreApplication::organizationName(),
                      QCoreApplication::applicationName());
  if (agreement.value(QStringLiteral("legal/agreed"), false).toBool())
    return;

  auto *dialog = new AgreementDialog(this);
  dialog->setWindowTitle(tr("Welcome"));
  dialog->setWindowModality(Qt::ApplicationModal);
  dialog->setMinimumSize(520, 470);
  dialog->resize(560, 540);

  auto *layout = new QVBoxLayout(dialog);

  auto *intro = new QTextBrowser(dialog);
  intro->setOpenExternalLinks(true);
  intro->setFocusPolicy(Qt::NoFocus);
  intro->setHtml(tr(
      "<h2>Welcome!</h2>"
      "<p>Thank you for choosing <b>LucidGrasp</b>. This repository on "
      "GitHub (<i>dialga-cmd/LucidGrasp</i>) is a free and open-source image "
      "search engine: it indexes a folder on your own computer and finds "
      "every visually similar image, entirely on your own machine.</p>"
      "<p>Before you start, we kindly ask you to read the legal documents "
      "that govern your use of the software: the <b>Privacy Policy</b>, the "
      "<b>Terms of Use</b>, the <b>Disclaimer of Warranties and Limitation "
      "of Liability</b> and the <b>Acceptable Use Policy</b>. They are "
      "deliberately super small — reading all of them would only take "
      "you about <b>10 minutes</b>. You can open them right here with the "
      "buttons below, or at any later time from the Help menu.</p>"
      "<p>We hope you enjoy LucidGrasp! If you are not ready to agree "
      "yet, close this window to exit — the Welcome dialog will "
      "return the next time you start the application.</p>"));
  layout->addWidget(intro, 1);

  auto *docsRow = new QHBoxLayout;
  auto *privacyButton = new QPushButton(tr("View &Privacy Policy..."), dialog);
  auto *legalButton = new QPushButton(tr("View &Legal Notices..."), dialog);
  connect(privacyButton, &QPushButton::clicked, this,
          &MainWindow::showPrivacyDialog);
  connect(legalButton, &QPushButton::clicked, this,
          &MainWindow::showLegalNoticesDialog);
  docsRow->addWidget(privacyButton);
  docsRow->addWidget(legalButton);
  docsRow->addStretch(1);
  layout->addLayout(docsRow);

  auto *agree = new QCheckBox(
      tr("I have read all the documents here and will comply with all of "
         "them"),
      dialog);
  layout->addWidget(agree);

  auto *enter = new QPushButton(tr("Enter"), dialog);
  enter->setObjectName(QStringLiteral("agreementEnterBtn"));
  enter->setMinimumWidth(120);
  enter->setDefault(true);
  enter->setEnabled(false);
  enter->setStyleSheet(QStringLiteral(
      "QPushButton#agreementEnterBtn { font-weight: 600; padding: 7px 18px; }"
      "QPushButton#agreementEnterBtn:disabled {"
      "  background-color: #c62828; border: 1px solid #8e1b1b;"
      "  color: #ffffff; }"
      "QPushButton#agreementEnterBtn:enabled {"
      "  background-color: #2e7d32; border: 1px solid #1b5e20;"
      "  color: #ffffff; }"
      "QPushButton#agreementEnterBtn:enabled:hover {"
      "  background-color: #388e3c; }"
      "QPushButton#agreementEnterBtn:enabled:pressed {"
      "  background-color: #1b5e20; }"));
  connect(agree, &QCheckBox::toggled, enter, &QPushButton::setEnabled);
  auto *agreeRow = new QHBoxLayout;
  agreeRow->addStretch(1);
  agreeRow->addWidget(enter);
  layout->addLayout(agreeRow);

  connect(enter, &QPushButton::clicked, dialog, [dialog] {
    QSettings stored(QSettings::IniFormat, QSettings::UserScope,
                     QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
    stored.setValue(QStringLiteral("legal/agreed"), true);
    dialog->accept();
  });

  dialog->exec();
  dialog->deleteLater();
}

void MainWindow::showPrivacyDialog() {
  auto *dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Privacy Policy"));
  dialog->setModal(true);
  dialog->resize(560, 600);

  auto *layout = new QVBoxLayout(dialog);
  auto *text = new QTextBrowser(dialog);
  text->setOpenExternalLinks(true);
  text->setPlainText(reflowLegalText(QString::fromLatin1(kPrivacyPolicyText)));
  layout->addWidget(text);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, dialog);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
  layout->addWidget(buttons);

  dialog->show();
}

void MainWindow::showLegalNoticesDialog() {
  auto *dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Legal Notices"));
  dialog->setModal(true);
  dialog->resize(620, 620);

  auto *layout = new QVBoxLayout(dialog);
  auto *tabs = new QTabWidget(dialog);

  auto *terms = new QTextBrowser(dialog);
  terms->setOpenExternalLinks(true);
  terms->setPlainText(reflowLegalText(QString::fromLatin1(kTermsText)));
  tabs->addTab(terms, tr("Terms of Use"));

  auto *disclaimer = new QTextBrowser(dialog);
  disclaimer->setOpenExternalLinks(true);
  disclaimer->setPlainText(
      reflowLegalText(QString::fromLatin1(kDisclaimerText)));
  tabs->addTab(disclaimer, tr("Warranty & Liability"));

  auto *acceptableUse = new QTextBrowser(dialog);
  acceptableUse->setOpenExternalLinks(true);
  acceptableUse->setPlainText(
      reflowLegalText(QString::fromLatin1(kAcceptableUseText)));
  tabs->addTab(acceptableUse, tr("Acceptable Use"));

  layout->addWidget(tabs);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, dialog);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
  layout->addWidget(buttons);

  dialog->show();
}

void MainWindow::showUpdateDialog(const QString &tag, const QString &url,
                                  const QString &notes) {
  auto *dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Update available"));
  dialog->setWindowModality(Qt::NonModal);
  dialog->resize(620, 560);

  auto *layout = new QVBoxLayout(dialog);

  auto *headline = new QLabel(
      tr("LucidGrasp %1 has been released.\nYou are running %2.")
          .arg(app::normaliseVersion(tag),
               app::UpdateChecker::currentVersion()),
      dialog);
  headline->setWordWrap(true);
  layout->addWidget(headline);

  if (!notes.trimmed().isEmpty()) {
    auto *notesView = new QTextBrowser(dialog);
    notesView->setOpenExternalLinks(true);
    notesView->setMarkdown(notes);
    notesView->setMinimumSize({560, 320});
    layout->addWidget(notesView, 1);
  }

  auto *buttons = new QDialogButtonBox(dialog);
  auto *open =
      buttons->addButton(tr("&Open Download Page"), QDialogButtonBox::AcceptRole);
  auto *later = buttons->addButton(tr("&Not Now"), QDialogButtonBox::RejectRole);
  auto *ignore = buttons->addButton(tr("&Ignore This Version"),
                                    QDialogButtonBox::ActionRole);
  auto *never = buttons->addButton(tr("Never Check for &Updates"),
                                   QDialogButtonBox::DestructiveRole);
  later->setDefault(true);
  layout->addWidget(buttons);

  connect(open, &QAbstractButton::clicked, dialog, [dialog, url] {
    QDesktopServices::openUrl(QUrl(url));
    dialog->accept();
  });
  connect(later, &QAbstractButton::clicked, dialog, &QDialog::accept);
  connect(ignore, &QAbstractButton::clicked, dialog, [this, dialog, tag] {
    updates_->setIgnoredVersion(tag);
    dialog->accept();
  });
  connect(never, &QAbstractButton::clicked, dialog, [this, dialog] {
    updates_->setDisabled(true);
    autoUpdateAction_->setChecked(false);
    dialog->accept();
  });
  connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);

  dialog->show();
}

void MainWindow::onUpdateCheckFinished(app::CheckOutcome outcome) {
  const bool wasManual = updateCheckManual_;
  updateCheckManual_ = false;

  if (outcome != app::CheckOutcome::UpToDate &&
      outcome != app::CheckOutcome::Unreachable)
    return;

  if (!wasManual)
    return;

  if (outcome == app::CheckOutcome::UpToDate)
    statusBar()->showMessage(
        tr("LucidGrasp %1 is up to date.")
            .arg(app::UpdateChecker::currentVersion()),
        6000);
  else if (outcome == app::CheckOutcome::Suppressed)
    statusBar()->showMessage(
        tr("The latest version is being ignored."), 6000);
  else
    statusBar()->showMessage(tr("Could not reach GitHub to check for updates."),
                             6000);
}

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
  if (indexing_ || searching_)
    return;

  const QString dir = askForDirectory(QStringLiteral("Select image library"));
  if (dir.isEmpty())
    return;

  libEdit_->setText(dir);
  tryLoadIndex(dir);
}

void MainWindow::tryLoadIndex(const QString &dir) {
  if (indexing_ || searching_)
    return;

  results_->clear();
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
    return;
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
    progress_->setRange(0, 0);
    stats_->setText(QStringLiteral("Scanning for images…\n%1").arg(current));
    return;
  }
  progress_->setRange(0, 100);
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
  indexBtn_->setEnabled(false);
  statusBar()->showMessage(
      QStringLiteral("Stopping... partial index will be discarded"), 4000);
}

void MainWindow::browseQuery() {
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
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QImage img = reader.read();
  if (img.isNull()) {
    preview_->setText(QStringLiteral("cannot load"));
    return;
  }
  preview_->setPixmap(QPixmap::fromImage(img).scaled(
      preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::startBackgroundRemoval() {
  if (bgBusy_ || indexing_ || searching_)
    return;

  const QString source =
      askForFile(QStringLiteral("Select image to cut out"), imageOpenFilter());
  if (source.isEmpty())
    return;

  QString model = core::defaultModelPath();
  if (model.isEmpty())
    model = ui::ensureBackgroundModel(this);
  if (model.isEmpty())
    return;

  bgBusy_ = true;
  setBusy(true);
  progress_->setRange(0, 0);
  statusBar()->showMessage(QStringLiteral("Removing background…"));

  bgWorker_ = std::thread([this, source, model] {
    QImage cutout;
    QString error;
    bool ok = false;
    try {
      ok = bgRemover_.isLoaded() && bgRemover_.modelPath() == model;
      if (!ok)
        ok = bgRemover_.loadModel(model, &error);
      if (ok)
        ok = bgRemover_.removeBackground(QImage(source), &cutout, &error);
    } catch (const std::exception &e) {
      ok = false;
      error = QString::fromUtf8(e.what());
    } catch (...) {
      ok = false;
      error = QStringLiteral("unknown error");
    }

    QMetaObject::invokeMethod(
        this,
        [this, ok, source, error, cutout = std::move(cutout)]() mutable {
          onBackgroundDone(ok, source, std::move(cutout), error);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::onBackgroundDone(bool ok, const QString &sourcePath,
                                  const QImage &cutout,
                                  const QString &error) {
  if (bgWorker_.joinable())
    bgWorker_.join();
  bgBusy_ = false;
  progress_->setRange(0, 100);
  setBusy(false);
  statusBar()->clearMessage();

  if (!ok) {
    const QString detail = error.isEmpty()
                               ? QStringLiteral("no output was produced")
                               : error;
    QMessageBox::warning(this, QStringLiteral("Remove background"),
                         QStringLiteral("Background removal failed:\n%1")
                             .arg(detail));
    return;
  }
  showBackgroundResult(sourcePath, cutout);
}

void MainWindow::showBackgroundResult(const QString &sourcePath,
                                      const QImage &cutout) {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Background removed"));
  dialog.resize(880, 540);

  auto *outer = new QVBoxLayout(&dialog);
  auto *content = new QHBoxLayout;
  outer->addLayout(content);

  const auto makeLabel = [](const QImage &image, int width) {
    auto *label = new QLabel;
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumSize(width, 360);
    label->setPixmap(QPixmap::fromImage(image).scaled(
        QSize(width, 360), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return label;
  };

  QImageReader reader(sourcePath);
  reader.setAutoTransform(true);
  const QImage original = reader.read();
  content->addWidget(makeLabel(original.isNull() ? cutout : original, 420));
  content->addWidget(makeLabel(checkerboardUnder(cutout), 420));

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  QPushButton *saveButton = buttons->addButton(
      QStringLiteral("Save As…"), QDialogButtonBox::ActionRole);
  connect(saveButton, &QPushButton::clicked, &dialog, [&] {
    const QFileInfo info(sourcePath);
    const QString base = info.completeBaseName();
    const QString target = QFileDialog::getSaveFileName(
        &dialog, QStringLiteral("Save cutout"),
        info.absolutePath() + QLatin1Char('/') + base +
            QStringLiteral("_cutout.png"),
        QStringLiteral("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (target.isEmpty())
      return;
    const bool jpeg = target.endsWith(QLatin1String(".jpg")) ||
                      target.endsWith(QLatin1String(".jpeg"));
    const QImage toSave = jpeg ? checkerboardUnder(cutout) : cutout;
    if (!toSave.save(target, jpeg ? "JPG" : "PNG")) {
      QMessageBox::warning(&dialog, QStringLiteral("Save cutout"),
                           QStringLiteral("Could not write %1.").arg(target));
      return;
    }
    dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  outer->addWidget(buttons);

  dialog.exec();
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

  const double threshold = thresholdSpin_->value() / 100.0;
  searchWorker_ = std::thread([this, query, threshold] {
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
    const int pct = int(qint64(done) * 100 / total);
    progress_->setValue(pct);
    stats_->setText(QStringLiteral("Searching %1%").arg(pct));
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
  if (cancel_) {
    stats_->setText(QStringLiteral("Search cancelled."));
    statusBar()->showMessage(QStringLiteral("Search cancelled"), 4000);
    return;
  }
  if (!ok) {
    stats_->setText(QStringLiteral("The query image could not be read."));
    statusBar()->showMessage(QStringLiteral("Query image unreadable"), 4000);
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
    const int pct = int(r.score * 100.0 + 0.5);

    QImageReader reader(r.absPath);
    reader.setAutoTransform(true);
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
    item->setData(Qt::UserRole + 1, r.relPath);
    item->setToolTip(
        r.absPath + QStringLiteral("\nscore: ") +
        QString::number(r.score, 'f', 3) +
        (r.exact ? QStringLiteral("\nbyte-identical copy") : QString()));
    item->setSizeHint({170, 200});
    results_->addItem(item);
  }
}

QListWidgetItem *MainWindow::currentResult() const {
  QListWidgetItem *item = results_->currentItem();
  if (!item || item->data(Qt::UserRole).toString().isEmpty())
    return nullptr;
  return item;
}

void MainWindow::revealResult() {
  QListWidgetItem *item = currentResult();
  if (!item)
    return;
  const QString path = item->data(Qt::UserRole).toString();

  const QDir dir = QFileInfo(path).absoluteDir();
  if (!dir.exists()) {
    QMessageBox::warning(this, QStringLiteral("Cannot show file"),
                         QStringLiteral("The folder is gone:\n%1")
                             .arg(QDir::toNativeSeparators(dir.path())));
    return;
  }

  QPointer<MainWindow> self(this);
  ui::revealInFileManager(path, [self, dir](ui::RevealOutcome outcome) {
    if (!self)
      return;
    switch (outcome) {
      case ui::RevealOutcome::Selected:
        break;
      case ui::RevealOutcome::FolderOnly:
        self->statusBar()->showMessage(
            QStringLiteral("Opened %1, but this file manager cannot preselect "
                           "a file.")
                .arg(QDir::toNativeSeparators(dir.path())),
            6000);
        break;
      case ui::RevealOutcome::Failed:
        self->statusBar()->showMessage(
            QStringLiteral("Could not open a file manager."), 6000);
        break;
    }
  });
}

void MainWindow::trashResult() {
  QListWidgetItem *item = currentResult();
  if (!item)
    return;
  const QString path = item->data(Qt::UserRole).toString();
  const QString name = QFileInfo(path).fileName();

  const auto answer = QMessageBox::question(
      this, QStringLiteral("Move to trash"),
      QStringLiteral("Move this file to the trash?\n\n%1").arg(name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes)
    return;

  QString error;
  if (!ui::moveToTrash(path, &error)) {
    QMessageBox::critical(this, QStringLiteral("Cannot delete"),
                          error.isEmpty()
                              ? QStringLiteral("The file could not be moved to "
                                               "the trash.")
                              : error);
    return;
  }

  index_.removeEntry(item->data(Qt::UserRole + 1).toString());

  const int row = results_->row(item);
  delete results_->takeItem(row);

  const bool cached = index_.save(core::defaultIndexPath(libEdit_->text()));

  statusBar()->showMessage(
      cached ? QStringLiteral("Moved to trash: %1").arg(name)
             : QStringLiteral("Moved to trash: %1 (index cache not saved)")
                   .arg(name),
      5000);
  updateResultActions();
}

void MainWindow::updateResultActions() {
  const bool haveResult = currentResult() != nullptr;
  revealBtn_->setEnabled(haveResult);
  trashBtn_->setEnabled(haveResult);
}

void MainWindow::setBusy(bool busy) {
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
  const bool busy = indexing_ || searching_ || bgBusy_;
  const bool canAct = !busy && currentResult() != nullptr;
  revealBtn_->setEnabled(canAct);
  trashBtn_->setEnabled(canAct);
  const bool canIndex = !busy && !libEdit_->text().isEmpty();
  indexBtn_->setEnabled(indexing_ || canIndex);
  browseLibBtn_->setEnabled(!busy);
  browseQueryBtn_->setEnabled(!busy);
  searchBtn_->setEnabled(
      busy ? searching_ : (!index_.empty() && !queryEdit_->text().isEmpty()));
  if (indexAction_)
    indexAction_->setEnabled(!busy);
  if (queryAction_)
    queryAction_->setEnabled(!busy);
  if (bgAction_)
    bgAction_->setEnabled(!busy);
}

bool MainWindow::systemPrefersDark() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
  if (scheme == Qt::ColorScheme::Dark)
    return true;
  if (scheme == Qt::ColorScheme::Light)
    return false;
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

QIcon MainWindow::paintTrashGlyph(int logicalSize, const QColor &ink,
                                 const QColor &faded) {
  const auto draw = [&](const QColor &colour) {
    const qreal s = logicalSize;
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap bin(qRound(s * dpr), qRound(s * dpr));
    bin.setDevicePixelRatio(dpr);
    bin.fill(Qt::transparent);

    QPainter p(&bin);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(dpr, dpr);
    p.setPen(QPen(colour, qMax(1.0, s * 0.09)));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(s * 0.34, s * 0.30), QPointF(s * 0.40, s * 0.16));
    p.drawLine(QPointF(s * 0.40, s * 0.16), QPointF(s * 0.60, s * 0.16));
    p.drawLine(QPointF(s * 0.60, s * 0.16), QPointF(s * 0.66, s * 0.30));
    p.drawLine(QPointF(s * 0.10, s * 0.30), QPointF(s * 0.90, s * 0.30));
    p.drawLine(QPointF(s * 0.18, s * 0.30), QPointF(s * 0.26, s * 0.88));
    p.drawLine(QPointF(s * 0.26, s * 0.88), QPointF(s * 0.74, s * 0.88));
    p.drawLine(QPointF(s * 0.74, s * 0.88), QPointF(s * 0.82, s * 0.30));
    p.drawLine(QPointF(s * 0.40, s * 0.46), QPointF(s * 0.42, s * 0.72));
    p.drawLine(QPointF(s * 0.60, s * 0.46), QPointF(s * 0.58, s * 0.72));
    p.end();
    return bin;
  };

  QIcon bin;
  bin.addPixmap(draw(ink), QIcon::Normal, QIcon::On);
  bin.addPixmap(draw(faded), QIcon::Disabled, QIcon::On);
  return bin;
}

void MainWindow::updateResultIcons() {
  QFileIconProvider provider;
  revealBtn_->setIcon(provider.icon(QFileIconProvider::Folder));

  const bool dark = relativeLuminance(palette().color(QPalette::Window)) <
                    relativeLuminance(palette().color(QPalette::WindowText));
  const QColor ink =
      dark ? QColor(0xE5, 0x6A, 0x6A) : QColor(0xC0, 0x36, 0x2C);
  const QColor faded = mixToward(ink, palette().color(QPalette::Window), 0.55);
  trashBtn_->setIcon(
      paintTrashGlyph(trashBtn_->iconSize().width(), ink, faded));
}

void MainWindow::updateThemeGlyph() {
  themeToggleBtn_->setIcon(
      paintThemeGlyph(darkMode_, palette().color(QPalette::Link),
                      themeToggleBtn_->iconSize().width()));
  themeToggleBtn_->setToolTip(darkMode_
                                  ? QStringLiteral("Switch to light mode")
                                  : QStringLiteral("Switch to dark mode"));
}

void MainWindow::equalizePanelHeights() {
  if (!libGroup_ || !queryGroup_ || panelsSized_)
    return;

  const int tallest = std::max(libGroup_->sizeHint().height(),
                               queryGroup_->sizeHint().height());
  libGroup_->setFixedHeight(tallest);
  queryGroup_->setFixedHeight(tallest);
  panelsSized_ = true;
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);
  equalizePanelHeights();

  QTimer::singleShot(500, this,
                     [this] { updates_->checkOnStartup(); });
}

void MainWindow::applyThemeTo(QWidget *target) const {
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

#themeToggleBtn, #revealBtn, #trashBtn { padding: 0; }

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
  pal.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);
  pal.setColor(QPalette::Disabled, QPalette::Text, c.muted);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
  target->setPalette(pal);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent *event) {
  const QList<QUrl> urls = event->mimeData()->urls();
  if (urls.isEmpty())
    return;

  QString libraryDir;
  QString queryImage;
  for (const QUrl &url : urls) {
    if (!url.isLocalFile())
      continue;
    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    if (!info.exists())
      continue;

    if (info.isDir()) {
      if (libraryDir.isEmpty())
        libraryDir = path;
    } else if (info.isFile() && core::isSupportedImage(path)) {
      if (queryImage.isEmpty())
        queryImage = path;
    }
  }

  if (!libraryDir.isEmpty()) {
    libEdit_->setText(libraryDir);
    tryLoadIndex(libraryDir);
  }
  if (!queryImage.isEmpty()) {
    queryEdit_->setText(queryImage);
    showPreview(queryImage);
    updateActions();
  }
  event->acceptProposedAction();
}

void MainWindow::applyTheme() {
  applyThemeTo(this);

  qApp->setPalette(palette());

  updateThemeGlyph();
  updateResultIcons();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched == results_ && event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
      QListWidgetItem *item = currentResult();
      if (item)
        openResult(item);
      return true;
    }
    if (keyEvent->key() == Qt::Key_Delete) {
      if (currentResult())
        trashResult();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}
