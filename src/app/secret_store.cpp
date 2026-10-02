#include "app/secret_store.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

QString defaultIniPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.config/LucidGrasp");
    return dir + QStringLiteral("/providers.ini");
}

// Owner read/write only. Applied after every write because QSettings rewrites
// the file and a rewrite can reset the mode. On non-Unix this is a no-op; the
// file still lives under the user's application config directory there.
void restrictToOwner(const QString& path)
{
#if defined(Q_OS_UNIX)
    QFile::setPermissions(path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#else
    Q_UNUSED(path);
#endif
}

constexpr int kToolTimeoutMs = 5000;

// Runs a keychain helper to completion. `stdinData` is written only when it is
// non-null, so a lookup does not send an empty body. Returns false on any
// failure to run; the caller treats that as "this layer is unavailable".
bool runTool(const QString& program, const QStringList& args,
             const QByteArray& stdinData, QByteArray* stdoutData, int* exitCode)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(kToolTimeoutMs))
        return false;
    if (!stdinData.isNull()) {
        p.write(stdinData);
        p.closeWriteChannel();
    }
    if (!p.waitForFinished(kToolTimeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return false;
    }
    if (stdoutData)
        *stdoutData = p.readAllStandardOutput();
    if (exitCode)
        *exitCode = p.exitCode();
    return p.exitStatus() == QProcess::NormalExit;
}

}  // namespace

namespace app {

SecretStore::SecretStore(const QString& iniPath)
    : iniPath_(iniPath.isEmpty() ? defaultIniPath() : iniPath)
{
}

QString SecretStore::environmentVariable(const QString& providerId)
{
    QString suffix = providerId.toUpper();
    for (QChar& c : suffix) {
        if (!c.isLetterOrNumber())
            c = QLatin1Char('_');
    }
    return QStringLiteral("LUCIDGRASP_KEY_") + suffix;
}

QString SecretStore::mask(const QString& key)
{
    if (key.isEmpty())
        return QString();
    QString out = QStringLiteral("••••••••");
    if (key.size() >= 8)
        out += key.right(4);
    return out;
}

bool SecretStore::hasValue(const QString& key)
{
    return !key.trimmed().isEmpty();
}

StoredSecret SecretStore::load(const QString& providerId) const
{
    if (providerId.isEmpty())
        return {};

    const QString env = environmentVariable(providerId);
    if (qEnvironmentVariableIsSet(env.toUtf8().constData())) {
        const QString value = qEnvironmentVariable(env.toUtf8().constData());
        if (!value.isEmpty())
            return {value, SecretBackend::Environment};
    }

    if (keychainEnabled_ && keychainAvailable()) {
        QString value;
        if (keychainLoad(providerId, &value) && !value.isEmpty())
            return {value, SecretBackend::Keychain};
    }

    return iniLoad(providerId);
}

bool SecretStore::save(const QString& providerId, const QString& key)
{
    if (providerId.isEmpty() || !hasValue(key))
        return false;

    if (keychainEnabled_ && keychainAvailable()
        && keychainSave(providerId, key))
        return true;

    return iniSave(providerId, key);
}

bool SecretStore::remove(const QString& providerId)
{
    if (providerId.isEmpty())
        return false;

    if (keychainEnabled_ && keychainAvailable())
        keychainRemove(providerId);  // best effort; the file layer still runs

    return iniRemove(providerId);
}

// --- INI fallback ----------------------------------------------------------

bool SecretStore::iniSave(const QString& providerId, const QString& key)
{
    const QString dir = QFileInfo(iniPath_).absolutePath();
    if (!QDir().mkpath(dir))
        return false;

    QSettings store(iniPath_, QSettings::IniFormat);
    store.setValue(QStringLiteral("keys/") + providerId, key);
    store.sync();
    if (store.status() != QSettings::NoError)
        return false;

    restrictToOwner(iniPath_);
    return true;
}

StoredSecret SecretStore::iniLoad(const QString& providerId) const
{
    QSettings store(iniPath_, QSettings::IniFormat);
    const QString value =
        store.value(QStringLiteral("keys/") + providerId).toString();
    if (value.isEmpty())
        return {};
    return {value, SecretBackend::Settings};
}

bool SecretStore::iniRemove(const QString& providerId)
{
    if (!QFileInfo::exists(iniPath_))
        return true;  // nothing stored, nothing to remove

    QSettings store(iniPath_, QSettings::IniFormat);
    store.remove(QStringLiteral("keys/") + providerId);
    store.sync();
    return store.status() == QSettings::NoError;
}

// --- Keychain layer --------------------------------------------------------

bool SecretStore::keychainAvailable() const
{
#if defined(Q_OS_LINUX)
    return !QStandardPaths::findExecutable(QStringLiteral("secret-tool"))
                .isEmpty();
#elif defined(Q_OS_MACOS)
    return !QStandardPaths::findExecutable(QStringLiteral("security"))
                .isEmpty();
#else
    return false;
#endif
}

bool SecretStore::keychainSave(const QString& providerId, const QString& key)
{
#if defined(Q_OS_LINUX)
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("secret-tool"),
        {QStringLiteral("store"),
         QStringLiteral("--label=LucidGrasp surface-scan key"),
         QStringLiteral("service"), QStringLiteral("lucidgrasp"),
         QStringLiteral("provider"), providerId},
        key.toUtf8(), nullptr, &code);
    return ran && code == 0;
#elif defined(Q_OS_MACOS)
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("security"),
        {QStringLiteral("add-generic-password"), QStringLiteral("-U"),
         QStringLiteral("-s"), QStringLiteral("lucidgrasp"),
         QStringLiteral("-a"), providerId, QStringLiteral("-w"), key},
        QByteArray(), nullptr, &code);
    return ran && code == 0;
#else
    Q_UNUSED(providerId);
    Q_UNUSED(key);
    return false;
#endif
}

bool SecretStore::keychainLoad(const QString& providerId, QString* out) const
{
#if defined(Q_OS_LINUX)
    QByteArray data;
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("secret-tool"),
        {QStringLiteral("lookup"), QStringLiteral("service"),
         QStringLiteral("lucidgrasp"), QStringLiteral("provider"), providerId},
        QByteArray(), &data, &code);
    if (!ran || code != 0)
        return false;
    if (out)
        *out = QString::fromUtf8(data).trimmed();
    return true;
#elif defined(Q_OS_MACOS)
    QByteArray data;
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("security"),
        {QStringLiteral("find-generic-password"), QStringLiteral("-s"),
         QStringLiteral("lucidgrasp"), QStringLiteral("-a"), providerId,
         QStringLiteral("-w")},
        QByteArray(), &data, &code);
    if (!ran || code != 0)
        return false;
    if (out)
        *out = QString::fromUtf8(data).trimmed();
    return true;
#else
    Q_UNUSED(providerId);
    Q_UNUSED(out);
    return false;
#endif
}

bool SecretStore::keychainRemove(const QString& providerId)
{
#if defined(Q_OS_LINUX)
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("secret-tool"),
        {QStringLiteral("clear"), QStringLiteral("service"),
         QStringLiteral("lucidgrasp"), QStringLiteral("provider"), providerId},
        QByteArray(), nullptr, &code);
    return ran && code == 0;
#elif defined(Q_OS_MACOS)
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("security"),
        {QStringLiteral("delete-generic-password"), QStringLiteral("-s"),
         QStringLiteral("lucidgrasp"), QStringLiteral("-a"), providerId},
        QByteArray(), nullptr, &code);
    return ran && code == 0;
#else
    Q_UNUSED(providerId);
    return false;
#endif
}

}  // namespace app
