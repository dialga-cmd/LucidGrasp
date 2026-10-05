#include "app/secret_store.h"

#include "app/providers.h"

#include <thread>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
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

StoredSecret SecretStore::cachedOrLoad(const QString& providerId) const
{
    // The environment layer is checked ahead of the cache, every time. An
    // override is the thing a user or a CI run flips at will, so it must never
    // go stale behind a value cached a moment ago, and it must never be cached
    // itself -- a cached override would outlive the variable and survive being
    // unset. Two reasons to keep it out of cache_ entirely.
    const StoredSecret env = environmentOverride(providerId);
    if (env.found())
        return env;

    // The two persistent layers. A hit here is what keeps a request handler off
    // the helper program, and the preload is what puts the entries there in the
    // first place -- see preloadAsync().
    {
        QMutexLocker lock(&cacheMutex_);
        if (cache_.contains(providerId))
            return cache_.value(providerId);
    }

    const StoredSecret secret = resolvePersistent(providerId);
    cacheStore(providerId, secret);
    return secret;
}

StoredSecret SecretStore::load(const QString& providerId) const
{
    if (providerId.isEmpty())
        return {};
    return cachedOrLoad(providerId);
}

// The read-only override layer, in one place. Deliberately not folded into
// resolvePersistent(): the two have different caching rules, and folding them
// together is exactly how an override ends up behind a cached file value.
StoredSecret SecretStore::environmentOverride(const QString& providerId) const
{
    const QByteArray env = environmentVariable(providerId).toUtf8();
    if (!qEnvironmentVariableIsSet(env.constData()))
        return {};
    const QString value = qEnvironmentVariable(env.constData());
    if (value.isEmpty())
        return {};
    return {value, SecretBackend::Environment};
}

// The two cached layers, keychain over INI. Used by the cache-miss path and by
// the preload thread alike, so the precedence between them cannot drift.
StoredSecret SecretStore::resolvePersistent(const QString& providerId) const
{
    if (keychainEnabled_ && keychainAvailable()) {
        QString value;
        if (keychainLoad(providerId, &value) && !value.isEmpty())
            return {value, SecretBackend::Keychain};
    }

    return iniLoad(providerId);
}

void SecretStore::cacheStore(const QString& providerId,
                             const StoredSecret& secret) const
{
    QMutexLocker lock(&cacheMutex_);
    cache_.insert(providerId, secret);
}

void SecretStore::cacheForget(const QString& providerId) const
{
    QMutexLocker lock(&cacheMutex_);
    cache_.remove(providerId);
}

SecretStore::~SecretStore()
{
    preloadCancelled_.store(true);
    if (preloadThread_.joinable())
        preloadThread_.join();
}

void SecretStore::preloadAsync()
{
    // One warm-up at a time. Starting a second thread would just race the first
    // one into the same cache, and the second would win the race to be joined
    // at shutdown while the first kept reading members already gone.
    if (preloadThread_.joinable())
        return;

    preloadCancelled_.store(false);
    preloadThread_ = std::thread([this] {
        for (const Provider& provider : providers()) {
            if (preloadCancelled_.load())
                return;
            cacheStore(provider.id, resolvePersistent(provider.id));
        }
    });
}

bool SecretStore::save(const QString& providerId, const QString& key)
{
    // Mutates the cache, so it is written here rather than through the const
    // accessors; load() stays const because resolving a secret does not.
    if (providerId.isEmpty() || !hasValue(key))
        return false;

    // Whatever layer takes it, the cache is updated here rather than being
    // invalidated: re-resolving would put a helper-program round trip straight
    // back onto the GUI thread that this cache exists to keep clear.
    if (keychainEnabled_ && keychainAvailable()
        && keychainSave(providerId, key)) {
        cacheStore(providerId, StoredSecret{key, SecretBackend::Keychain});
        return true;
    }

    if (!iniSave(providerId, key))
        return false;
    cacheStore(providerId, StoredSecret{key, SecretBackend::Settings});
    return true;
}

bool SecretStore::remove(const QString& providerId)
{
    if (providerId.isEmpty())
        return false;

    if (keychainEnabled_ && keychainAvailable())
        keychainRemove(providerId);  // best effort; the file layer still runs

    cacheForget(providerId);
    return iniRemove(providerId);
}

// --- INI fallback ----------------------------------------------------------

bool SecretStore::iniSave(const QString& providerId, const QString& key)
{
    // Serialises the whole INI layer. QSettings keeps per-file state, and two
    // threads opening the same file interleave their writes -- a lost update on
    // a user's key file is not a tolerable outcome for a performance tweak, so
    // this lock is not optional now that preloadAsync() reads the file from a
    // worker thread.
    QMutexLocker lock(&iniMutex_);

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
    QMutexLocker lock(&iniMutex_);

    QSettings store(iniPath_, QSettings::IniFormat);
    const QString value =
        store.value(QStringLiteral("keys/") + providerId).toString();
    if (value.isEmpty())
        return {};
    return {value, SecretBackend::Settings};
}

bool SecretStore::iniRemove(const QString& providerId)
{
    QMutexLocker lock(&iniMutex_);

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
    // The key is written to stdin, never passed as an argument. On Unix the
    // argument vector of a running process is world-readable through ps(1), so
    // `security ... -w <key>` handed the provider key to every other user on the
    // machine for as long as the process lived. Apple's own documentation calls
    // the valueless form the recommended one: "-w password  Specify password to
    // be added. Put at end of command to be prompted". Valued, it is a leak.
    //
    // The password is supplied twice because `security` verifies the typed entry
    // before storing it, and which of the two reads happens is a detail of the
    // tool rather than something documented. Sending it twice is correct either
    // way: a prompt that reads once ignores the extra line, and a prompt that
    // reads twice gets what it asked for. stdin is a pipe here, not a terminal,
    // so nothing is echoed back.
    const QByteArray payload =
        key.toUtf8() + '\n' + key.toUtf8() + '\n';
    int code = -1;
    const bool ran = runTool(
        QStringLiteral("security"),
        {QStringLiteral("add-generic-password"), QStringLiteral("-U"),
         QStringLiteral("-s"), QStringLiteral("lucidgrasp"),
         QStringLiteral("-a"), providerId,
         // Must stay last: it is what triggers the prompt.
         QStringLiteral("-w")},
        payload, nullptr, &code);
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
