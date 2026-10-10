#include "ui/model_download.h"

#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {

namespace {

struct ModelInfo {
  const char *tag;
  const char *fileName;
  const char *url;
  const char *md5;
  const char *displayName;
  qint64 sizeBytes;
  int revision;
};

const ModelInfo kModelInfo[] = {
    {"lite",
     "birefnet-general-lite.onnx",
     "https://github.com/danielgatis/rembg/releases/download/v0.0.0/"
     "BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx",
     "4fab47adc4ff364be1713e97b7e66334",
     "Lite model (BiRefNet tiny)",
     224005088,
     1},
    {"general",
     "birefnet-general.onnx",
     "https://github.com/danielgatis/rembg/releases/download/v0.0.0/"
     "BiRefNet-general-epoch_244.onnx",
     "7a35a0141cbbc80de11d9c9a28f52697",
     "General model (BiRefNet)",
     972666916,
     1},
    {"semantic",
     "dinov2_small_int8.onnx",
     "https://huggingface.co/onnx-community/dinov2-small/resolve/"
     "8b1f705a3a7f6f062f6bdd21986c1583d3ef105d/onnx/model_int8.onnx",
     "70279b6f33ef8a85966ef8f8493a3f2b",
     "Semantic model (DINOv2 small)",
     24446700,
     1},
};

const ModelInfo &infoFor(SearchModel model)
{
  return kModelInfo[static_cast<int>(model)];
}

QString modelsDir()
{
  return QStandardPaths::writableLocation(
             QStandardPaths::AppLocalDataLocation) +
         QStringLiteral("/models");
}

QString sizeLabel(qint64 bytes)
{
  if (bytes >= 1048576)
    return QStringLiteral("%1 MB").arg(bytes / 1048576);
  if (bytes >= 1024)
    return QStringLiteral("%1 KB").arg(bytes / 1024);
  return QStringLiteral("%1 B").arg(bytes);
}

void recordDownloadedRevision(const ModelInfo &info)
{
  QSettings settings;
  settings.beginGroup(QStringLiteral("models"));
  settings.setValue(QLatin1String(info.tag) + QStringLiteral("/revision"),
                    info.revision);
  settings.sync();
}

// Interactive download with a log view. Runs in two phases: a prompt offering
// to cancel the switch or download the missing/outdated model, then a
// download phase. The dialog is accepted (and the switch applied) only when
// the model ends up ready.
class ModelSetupDialog : public QDialog {
  Q_OBJECT

public:
  ModelSetupDialog(SearchModel model, QWidget *parent)
      : QDialog(parent), model_(model)
  {
    setWindowTitle(tr("Search model"));
    setWindowModality(Qt::WindowModal);
    resize(520, 340);

    auto *layout = new QVBoxLayout(this);

    info_ = new QLabel(this);
    info_->setWordWrap(true);
    layout->addWidget(info_);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setFixedHeight(168);
    layout->addWidget(log_);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    layout->addWidget(progress_);

    box_ = new QDialogButtonBox(this);
    action_ = box_->addButton(actionText(), QDialogButtonBox::ActionRole);
    cancel_ = box_->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    ok_ = box_->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
    ok_->setVisible(false);
    cancel_->setDefault(true);
    layout->addWidget(box_);

    connect(action_, &QPushButton::clicked, this,
            &ModelSetupDialog::startDownload);
    connect(cancel_, &QPushButton::clicked, this, &ModelSetupDialog::reject);
    connect(ok_, &QPushButton::clicked, this, &ModelSetupDialog::accept);

    const ModelInfo &info = infoFor(model_);
    switch (checkModelStatus(model_)) {
      case ModelStatus::Missing:
        info_->setText(tr("The %1 has not been downloaded yet. Model files "
                          "are not shipped with LucidGrasp; this one is about "
                          "%2 and will be fetched over the internet. "
                          "Download it now?")
                           .arg(QLatin1String(info.displayName))
                           .arg(sizeLabel(info.sizeBytes)));
        break;
      case ModelStatus::Corrupt:
        info_->setText(tr("The %1 file is present but looks damaged. Download "
                          "it again to repair this search mode?")
                           .arg(QLatin1String(info.displayName)));
        break;
      case ModelStatus::UpdateAvailable:
        info_->setText(tr("A newer revision of the %1 is available for this "
                          "build of LucidGrasp. Update it now?")
                           .arg(QLatin1String(info.displayName)));
        break;
      case ModelStatus::Ready:
        info_->setText(QString());
        break;
    }

    log(QStringLiteral("Selected: %1 (%2)")
            .arg(QLatin1String(info.displayName),
                 sizeLabel(info.sizeBytes)));
  }

  bool isReady() const { return ready_; }

private slots:
  void startDownload()
  {
    const ModelInfo &info = infoFor(model_);
    const QString target = modelStorePath(model_);
    const QString partial = target + QStringLiteral(".part");

    action_->setVisible(false);
    ok_->setVisible(false);
    progress_->setVisible(true);
    progress_->setRange(0, 0);
    progress_->setValue(0);
    info_->setText(tr("Downloading %1…").arg(QLatin1String(info.displayName)));

    QDir().mkpath(modelsDir());
    QFile::remove(partial);
    QFile out(partial);
    if (!out.open(QIODevice::WriteOnly)) {
      log(QStringLiteral("Error: cannot write %1").arg(partial));
      finishFailed();
      return;
    }

    log(QStringLiteral("Requesting %1").arg(QLatin1String(info.url)));

    QNetworkAccessManager manager;
    QNetworkReply *reply =
        manager.get(QNetworkRequest(QUrl(QString::fromUtf8(info.url))));

    QCryptographicHash hash(QCryptographicHash::Md5);
    int lastLogPercent = -1;
    bool failed = false;
    bool aborted = false;
    QString failure;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, &lastLogPercent](qint64 done, qint64 total) {
              if (total <= 0) {
                progress_->setRange(0, 0);
                return;
              }
              progress_->setRange(0, 100);
              const int percent = static_cast<int>(done * 100 / total);
              progress_->setValue(percent);
              if (lastLogPercent < 0 || percent - lastLogPercent >= 10) {
                log(QStringLiteral("%1% (%2 of %3)")
                        .arg(percent)
                        .arg(sizeLabel(done))
                        .arg(sizeLabel(total)));
                lastLogPercent = percent;
              }
            });
    connect(reply, &QNetworkReply::readyRead, [reply, &out, &hash] {
      const QByteArray chunk = reply->readAll();
      hash.addData(chunk);
      out.write(chunk);
    });
    connect(reply, &QNetworkReply::finished,
            [this, reply, &out, &hash, &failed, &failure, &info] {
              if (reply->error() != QNetworkReply::NoError) {
                failed = true;
                failure = reply->errorString();
                return;
              }
              log(QStringLiteral("Downloaded %1").arg(sizeLabel(out.size())));
              log(QStringLiteral("Verifying checksum…"));
              if (hash.result().toHex() != QByteArray(info.md5)) {
                failed = true;
                failure = QStringLiteral("checksum mismatch");
              }
            });

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(cancel_, &QPushButton::clicked, [&loop, &aborted] {
      aborted = true;
      loop.quit();
    });
    loop.exec();

    out.close();
    reply->deleteLater();

    if (aborted) {
      QFile::remove(partial);
      log(QStringLiteral("Cancelled."));
      reject();
      return;
    }
    if (failed) {
      QFile::remove(partial);
      log(QStringLiteral("Model download failed: %1").arg(failure));
      finishFailed();
      return;
    }
    if (!QFile::rename(partial, target)) {
      QFile::remove(partial);
      log(QStringLiteral("Error: could not store the model at %1")
              .arg(target));
      finishFailed();
      return;
    }

    recordDownloadedRevision(info);
    ready_ = true;
    progress_->setRange(0, 100);
    progress_->setValue(100);
    info_->setText(tr("The %1 is ready.")
                       .arg(QLatin1String(info.displayName)));
    log(QStringLiteral("Saved to %1").arg(target));
    log(QStringLiteral("Done. The model is ready to use."));
    cancel_->setVisible(false);
    ok_->setText(tr("Done"));
    ok_->setObjectName(QStringLiteral("modelOkBtn"));
    ok_->setVisible(true);
    ok_->setEnabled(true);
    ok_->setDefault(true);
  }

private:
  QString actionText() const
  {
    switch (checkModelStatus(model_)) {
      case ModelStatus::Missing:
        return tr("Download Model");
      case ModelStatus::Corrupt:
        return tr("Repair Model");
      case ModelStatus::UpdateAvailable:
        return tr("Update Model");
      case ModelStatus::Ready:
        break;
    }
    return tr("Download Model");
  }

  void finishFailed()
  {
    cancel_->setVisible(false);
    ok_->setVisible(true);
    ok_->setEnabled(true);
    ok_->setDefault(true);
  }

  void log(const QString &line)
  {
    log_->appendPlainText(line);
    log_->moveCursor(QTextCursor::End);
    log_->ensureCursorVisible();
  }

  SearchModel model_;
  QLabel *info_ = nullptr;
  QPlainTextEdit *log_ = nullptr;
  QProgressBar *progress_ = nullptr;
  QDialogButtonBox *box_ = nullptr;
  QPushButton *action_ = nullptr;
  QPushButton *cancel_ = nullptr;
  QPushButton *ok_ = nullptr;
  bool ready_ = false;
};

}  // namespace

QString modelStorePath(SearchModel model)
{
  return modelsDir() + QLatin1Char('/') +
         QLatin1String(infoFor(model).fileName);
}

QString modelDisplayName(SearchModel model)
{
  return QLatin1String(infoFor(model).displayName);
}

QString backgroundModelStorePath()
{
  return modelStorePath(SearchModel::General);
}

ModelStatus checkModelStatus(SearchModel model)
{
  const ModelInfo &info = infoFor(model);
  const QFileInfo fileInfo(modelStorePath(model));
  if (!fileInfo.exists() || fileInfo.size() <= 0)
    return ModelStatus::Missing;
  if (fileInfo.size() != info.sizeBytes)
    return ModelStatus::Corrupt;

  QSettings settings;
  settings.beginGroup(QStringLiteral("models"));
  const int storedRevision =
      settings.value(QLatin1String(info.tag) + QStringLiteral("/revision"), 0)
          .toInt();
  if (storedRevision < info.revision)
    return ModelStatus::UpdateAvailable;
  return ModelStatus::Ready;
}

bool prepareModelForSearch(SearchModel model, QWidget *parent)
{
  if (checkModelStatus(model) == ModelStatus::Ready)
    return true;

  ModelSetupDialog dialog(model, parent);
  if (dialog.exec() != QDialog::Accepted)
    return false;
  return dialog.isReady();
}

QString ensureBackgroundModel(QWidget *parent)
{
  if (checkModelStatus(SearchModel::General) == ModelStatus::Ready)
    return backgroundModelStorePath();
  if (prepareModelForSearch(SearchModel::General, parent))
    return backgroundModelStorePath();
  return QString();
}

}  // namespace ui

#include "model_download.moc"