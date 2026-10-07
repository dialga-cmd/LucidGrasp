#include "ui/file_actions.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#if defined(Q_OS_LINUX) || defined(Q_OS_BSD)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#endif

#if defined(Q_OS_WIN)
#include <QDesktopServices>
#include <QProcess>
#elif defined(Q_OS_MACOS)
#include <QDesktopServices>
#include <QProcess>
#else
#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>
#endif

namespace ui {

namespace {

void openContainingFolder(const QString &path) {
  QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void deliver(const RevealDone &done, RevealOutcome outcome) {
  QTimer::singleShot(0, [done, outcome] {
    if (done)
      done(outcome);
  });
}

void launchFallback(const QString &path, const RevealDone &done) {
  openContainingFolder(path);
  deliver(done, RevealOutcome::FolderOnly);
}

}

void revealInFileManager(const QString &path, const RevealDone &done) {
  const QFileInfo info(path);
  if (!info.exists()) {
    deliver(done, RevealOutcome::Failed);
    return;
  }

#if defined(Q_OS_WIN)
  const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
  if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                              QStringList{QStringLiteral("/select,%1").arg(native)}))
    deliver(done, RevealOutcome::Selected);
  else
    launchFallback(path, done);

#elif defined(Q_OS_MACOS)
  if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                              QStringList{QStringLiteral("-R"), path}))
    deliver(done, RevealOutcome::Selected);
  else
    launchFallback(path, done);

#else
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    launchFallback(path, done);
    return;
  }

  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.FileManager1"),
      QStringLiteral("/org/freedesktop/FileManager1"),
      QStringLiteral("org.freedesktop.FileManager1"),
      QStringLiteral("ShowItems"));
  call << QStringList{QUrl::fromLocalFile(path).toString()} << QString();

  QDBusPendingCall pending = bus.asyncCall(call, 5000);

  auto *watcher = new QDBusPendingCallWatcher(pending);
  QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                   [path, done](QDBusPendingCallWatcher *w) {
                     if (!w->isError())
                       deliver(done, RevealOutcome::Selected);
                     else
                       launchFallback(path, done);
                     w->deleteLater();
                   });
#endif
}

bool moveToTrash(const QString &path, QString *error) {
  const auto fail = [error](const QString &why) {
    if (error)
      *error = why;
    return false;
  };

  const QFileInfo info(path);
  if (!info.exists())
    return fail(QStringLiteral("The file no longer exists:\n%1")
                    .arg(QDir::toNativeSeparators(path)));
  if (!info.isWritable())
    return fail(QStringLiteral("This file is read-only, so it cannot be moved "
                               "to the trash."));
  if (info.isDir())
    return fail(QStringLiteral("This is a folder, not an image file."));

#if defined(Q_OS_WIN)
  const QString script =
      QStringLiteral("Add-Type -AssemblyName Microsoft.VisualBasic; "
                     "[Microsoft.VisualBasic.FileIO.FileSystem]::"
                     "DeleteFile('%1', "
                     "[Microsoft.VisualBasic.FileIO.UIOption]::"
                     "OnlyErrorDialogs, "
                     "[Microsoft.VisualBasic.FileIO.RecycleOption]::"
                     "SendToRecycleBin)")
          .arg(QString(path).replace(QLatin1Char('\''), QLatin1String("''")));

  QProcess p;
  p.start(QStringLiteral("powershell.exe"),
          {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
           QStringLiteral("-Command"), script});
  if (!p.waitForFinished(15000))
    return fail(QStringLiteral("The system trash could not be reached."));
  if (p.exitCode() != 0)
    return fail(QStringLiteral("Windows refused to move the file to the "
                               "Recycle Bin."));
  return true;

#elif defined(Q_OS_MACOS)
  QProcess p;
  p.start(QStringLiteral("/usr/bin/osascript"),
          {QStringLiteral("-e"),
           QStringLiteral("on run argv\n"
                          "  set theFile to POSIX file (item 1 of argv)\n"
                          "  tell application \"Finder\" to delete theFile\n"
                          "end run"),
           path});
  if (!p.waitForFinished(15000))
    return fail(QStringLiteral("The system trash could not be reached."));
  if (p.exitCode() != 0)
    return fail(QStringLiteral("macOS refused to move the file to the Trash."));
  return true;

#else
  const QString native = QDir::toNativeSeparators(path);

  struct Tool {
    QString program;
    QStringList args;
  };
  QVector<Tool> tools;
  const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
  if (!gio.isEmpty())
    tools.append({gio, {QStringLiteral("trash"), path}});
  const QString put =
      QStandardPaths::findExecutable(QStringLiteral("trash-put"));
  if (!put.isEmpty())
    tools.append({put, {native}});

  if (tools.isEmpty())
    return fail(QStringLiteral("No trash tool is available on this system. "
                               "Install gio or trash-cli to delete files "
                               "safely."));

  QString lastError;
  for (const Tool &tool : tools) {
    QProcess p;
    p.start(tool.program, tool.args);
    if (!p.waitForFinished(15000))
      continue;
    if (p.exitCode() == 0)
      return true;
    const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (!err.isEmpty())
      lastError = err;
  }

  return fail(lastError.isEmpty()
                  ? QStringLiteral("The file could not be moved to the trash.")
                  : QStringLiteral("The file could not be moved to the "
                                   "trash:\n%1")
                        .arg(lastError));
#endif
}

}