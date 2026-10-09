#include "ui/model_download.h"

#include "core/background_remover.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QUrl>

namespace ui {

namespace {

const char kModelUrl[] =
    "https://github.com/danielgatis/rembg/releases/download/v0.0.0/"
    "BiRefNet-general-epoch_244.onnx";
const char kModelMd5[] = "7a35a0141cbbc80de11d9c9a28f52697";

QString modelsDir()
{
  return QStandardPaths::writableLocation(
             QStandardPaths::AppLocalDataLocation) +
         QStringLiteral("/models");
}

QByteArray fileMd5(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();
  QCryptographicHash hash(QCryptographicHash::Md5);
  while (!file.atEnd())
    hash.addData(file.read(1 << 20));
  return hash.result().toHex();
}

}  // namespace

QString backgroundModelStorePath()
{
  return modelsDir() + QLatin1Char('/') + core::modelFileName();
}

QString ensureBackgroundModel(QWidget *parent)
{
  const QString path = backgroundModelStorePath();
  QDir().mkpath(modelsDir());

  if (QFileInfo::exists(path) && fileMd5(path) == QByteArray(kModelMd5))
    return path;
  QFile::remove(path);

  const QString partial = path + QStringLiteral(".part");
  QFile::remove(partial);
  QFile out(partial);
  if (!out.open(QIODevice::WriteOnly)) {
    QMessageBox::critical(
        parent, QStringLiteral("Download model"),
        QStringLiteral("Could not write to %1.").arg(partial));
    return QString();
  }

  QProgressDialog dialog(
      QStringLiteral("Downloading the background-removal model (about 1 GB)…"),
      QStringLiteral("Cancel"), 0, 0, parent);
  dialog.setWindowModality(Qt::WindowModal);
  dialog.setMinimumDuration(0);

  QNetworkAccessManager manager;
  QNetworkReply *reply =
      manager.get(QNetworkRequest(QUrl(QString::fromUtf8(kModelUrl))));

  QCryptographicHash hash(QCryptographicHash::Md5);
  bool cancelled = false;
  QString failure;

  QObject::connect(reply, &QNetworkReply::downloadProgress,
                   [&dialog](qint64 done, qint64 total) {
                     if (total <= 0) {
                       dialog.setRange(0, 0);
                       return;
                     }
                     dialog.setRange(0, 100);
                     dialog.setValue(static_cast<int>(done * 100 / total));
                   });
  QObject::connect(&dialog, &QProgressDialog::canceled,
                   [reply, &cancelled] {
                     cancelled = true;
                     reply->abort();
                   });
  QObject::connect(reply, &QNetworkReply::readyRead, [reply, &out, &hash] {
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
      return;
    hash.addData(chunk);
    out.write(chunk);
  });
  QObject::connect(reply, &QNetworkReply::finished,
                   [reply, &failure, &hash] {
                     if (reply->error() != QNetworkReply::NoError)
                       failure = reply->errorString();
                     else if (hash.result().toHex() != QByteArray(kModelMd5))
                       failure = QStringLiteral("checksum mismatch");
                   });

  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&dialog, &QProgressDialog::canceled, &loop, &QEventLoop::quit);
  loop.exec();

  out.close();
  reply->deleteLater();

  if (cancelled) {
    QFile::remove(partial);
    return QString();
  }
  if (!failure.isEmpty()) {
    QFile::remove(partial);
    QMessageBox::warning(parent, QStringLiteral("Download model"),
                         QStringLiteral("Failed to download the model: %1")
                             .arg(failure));
    return QString();
  }
  if (!QFile::rename(partial, path)) {
    QFile::remove(partial);
    QMessageBox::warning(parent, QStringLiteral("Download model"),
                         QStringLiteral("Could not store the model at %1.")
                             .arg(path));
    return QString();
  }
  return path;
}

}  // namespace ui