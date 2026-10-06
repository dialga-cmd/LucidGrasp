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

// Opens the containing folder with no selection. Used both as the fallback for
// platforms that cannot select, and as the "something went wrong, at least show
// them the folder" path.
void openContainingFolder(const QString &path) {
  QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

// Hands the outcome back on the event loop rather than inline.
//
// Every path goes through here, including the ones that already know the answer
// by the time they are asked. That is deliberate: a callback which fires
// sometimes before the call returns and sometimes after is a trap for whoever
// maintains this next, and the cost of deferring is one turn of the loop.
void deliver(const RevealDone &done, RevealOutcome outcome) {
  QTimer::singleShot(0, [done, outcome] {
    if (done)
      done(outcome);
  });
}

// Shown when the file manager would not select the file but its folder can
// still be opened. Not an error: the user gets their folder either way, so this
// reports FolderOnly rather than Failed.
void launchFallback(const QString &path, const RevealDone &done) {
  openContainingFolder(path);
  deliver(done, RevealOutcome::FolderOnly);
}

}  // namespace

void revealInFileManager(const QString &path, const RevealDone &done) {
  const QFileInfo info(path);
  if (!info.exists()) {
    deliver(done, RevealOutcome::Failed);
    return;
  }

#if defined(Q_OS_WIN)
  // Explorer only understands "/select,<path>" as a raw command line, and it
  // needs the native separators with the comma intact -- going through a URL
  // loses the select verb and just opens the folder.
  const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
  if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                              QStringList{QStringLiteral("/select,%1").arg(native)}))
    deliver(done, RevealOutcome::Selected);
  else
    launchFallback(path, done);

#elif defined(Q_OS_MACOS)
  // -R reveals, and unlike opening the folder it puts the file in the selection
  // and scrolls to it.
  if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                              QStringList{QStringLiteral("-R"), path}))
    deliver(done, RevealOutcome::Selected);
  else
    launchFallback(path, done);

#else
  // org.freedesktop.FileManager1 is the freedesktop.org standard for exactly
  // this, and its ShowItems method is specified to open the parent folder with
  // the given URIs selected. Implemented by the file managers themselves --
  // Nautilus 3.4+ and Dolphin 15.08+ -- which is why there is no package to
  // install: the capability already ships inside the file manager.
  //
  // This is also why D-Bus rather than simulated keystrokes. The bus is
  // session-level, so it works identically under X11 and Wayland, and it does
  // not have to steal focus or guess at a window.
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    // No session bus at all: an SSH session, a container, or a bare
    // VT. Nothing can be asked to select anything here.
    launchFallback(path, done);
    return;
  }

  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.FileManager1"),
      QStringLiteral("/org/freedesktop/FileManager1"),
      QStringLiteral("org.freedesktop.FileManager1"),
      QStringLiteral("ShowItems"));
  // Second argument is a startup notification id, unused here. Nautilus has a
  // documented quirk for this method when handed a *folder* URI -- it opens the
  // folder's contents instead of selecting it -- so the caller must only pass
  // file URIs. It is always a file here.
  call << QStringList{QUrl::fromLocalFile(path).toString()} << QString();

  // Async with a bounded timeout, never the blocking form: a file manager that
  // is busy indexing or genuinely wedged would otherwise block this window for
  // the full timeout, which is long enough to look like a crash.
  QDBusPendingCall pending = bus.asyncCall(call, 5000);

  auto *watcher = new QDBusPendingCallWatcher(pending);
  QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                   [path, done](QDBusPendingCallWatcher *w) {
                     // Any error at all -- name not activatable, interface
                     // absent, timeout -- degrades to opening the folder rather
                     // than to nothing. The folder is almost always useful even
                     // when the selection is not.
                     if (!w->isError())
                       deliver(done, RevealOutcome::Selected);
                     else
                       launchFallback(path, done);
                     w->deleteLater();
                   });
  // Deliberately nothing returned: the D-Bus reply has not arrived yet, so the
  // callback is the only place the outcome is genuinely known.
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
  // A directory would need the recursive variant, and the trash button is only
  // ever wired to image files. Refused rather than half-handled.
  if (info.isDir())
    return fail(QStringLiteral("This is a folder, not an image file."));

#if defined(Q_OS_WIN)
  // PowerShell rather than a direct shell call: it is present on every supported
  // Windows, and the .NET Microsoft.VisualBasic.FileIO.FileSystem method is the
  // documented way to reach the Recycle Bin. UIOption::OnlyErrorDialogs keeps it
  // silent -- the app has already asked the user, and a second confirmation
  // dialog from a helper process is noise.
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
  // NSFileManager's trashItemAtURL is the supported route, and no command-line
  // tool reaches it, so osascript asks Finder to do it instead.
  //
  // The path is passed as an argv element, not interpolated into the script.
  // osascript hands everything after the script to `on run argv`, so the path
  // arrives as a plain string and never goes through the AppleScript parser: a
  // filename containing a quote, a backslash, or the words "end run" cannot
  // change what runs. QProcess hands the argument vector straight to exec, so
  // there is no shell to re-parse it either.
  //
  // The previous version spliced the path into the script with %1 and escaped
  // backslashes and quotes by hand. That was more code for a weaker guarantee,
  // and it did not compile: `apath` was declared const and QString::replace() has
  // no const overload. It passed every review because the whole Q_OS_MACOS
  // branch is preprocessed out on both Linux and Windows, so nothing compiled it
  // until the macOS runner first ran.
  //
  // `POSIX file` is coerced in a `set` outside the tell block on purpose:
  // inside one the coercion could resolve against Finder's own terminology
  // instead of the standard addition, and the failure would then read as Finder
  // refusing the delete.
  //
  // First use on a Mac raises a system prompt asking permission for osascript to
  // control Finder. That is macOS's automation policy and no Finder-based route
  // avoids it; it is not an error, and refusing it makes this branch report the
  // failure below.
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
  // The freedesktop.org trash spec has no single owning command, so the
  // standard implementations are tried in turn. gio is the one that ships with
  // the widest range of desktops; trash-put is the reference implementation and
  // covers setups without gio. Anything else is refused rather than replaced
  // with an unlink, so the user is never told "deleted" when the file was
  // actually destroyed.
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
      continue;  // timed out; try the next one
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

}  // namespace ui