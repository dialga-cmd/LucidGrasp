#pragma once

#include <atomic>
#include <thread>

#include <QHash>
#include <QMutex>
#include <QString>

namespace app {

// Where a key was actually read from, in precedence order. The page shows this
// so a user can tell whether an override is coming from the environment, the
// desktop keyring, or the fallback file.
enum class SecretBackend {
    None,         // nothing stored
    Environment,  // LUCIDGRASP_KEY_<PROVIDER> (read-only override)
    Keychain,     // OS keyring (Secret Service / macOS Keychain)
    Settings,     // private INI file, owner-only permissions on Unix
};

struct StoredSecret {
    QString value;
    SecretBackend backend = SecretBackend::None;
    bool found() const { return backend != SecretBackend::None; }
};

// Layered storage for the surface-scan provider keys:
//
//   environment  >  OS keychain  >  private INI file
//
// The environment layer is read-only and exists so a user (or CI) can override
// without touching the file store. The keychain layer is best-effort: it is
// only consulted when its helper tool is present, and a failed write falls
// through to the INI file rather than losing the key. The INI layer is the
// guaranteed fallback and is written owner-only on Unix.
//
// The security boundary is the process, not the page: the browser is never
// told a key, only a mask, and it is the only consumer of this class.
class SecretStore {
public:
    // Empty path = the per-user application config directory, so the real app
    // never has to hardcode a location. Tests pass a scratch path.
    explicit SecretStore(const QString& iniPath = QString());

    // Not copyable: it owns a worker thread (see preloadAsync) and a mutex, and
    // copying either would give two objects pointing at one cache.
    SecretStore(const SecretStore&) = delete;
    SecretStore& operator=(const SecretStore&) = delete;

    // Waits for a running preload before letting go of the cache. The store is a
    // by-value member of the window, so it is destroyed on the way out of the
    // app, and a preload still in flight would otherwise be reading freed
    // members. The wait is bounded by a single provider lookup rather than all
    // ten, because the thread watches for the cancellation.
    ~SecretStore();

    // Precedence is resolved here, once, so callers cannot accidentally read a
    // lower layer first.
    StoredSecret load(const QString& providerId) const;

    // Writes to the keychain if it can, otherwise to the INI file. Returns
    // false only when neither layer accepted the value.
    bool save(const QString& providerId, const QString& key);

    // Removes from every writable layer. An environment override cannot be
    // removed and is deliberately left alone.
    bool remove(const QString& providerId);

    QString iniPath() const { return iniPath_; }

    // Environment variable backing a provider, e.g.
    // "google_vision" -> "LUCIDGRASP_KEY_GOOGLE_VISION".
    static QString environmentVariable(const QString& providerId);

    // Never returns the full key: a fixed mask plus, for keys long enough to
    // have a recognisable tail, the last four characters.
    static QString mask(const QString& key);

    // A cheap "is this worth storing" check. Not validation: real validation is
    // a signed network call (see KeyEntryServer).
    static bool hasValue(const QString& key);

    // Test seam only. Kept off the real desktop keyring in the self-test so a
    // run cannot read or write a user's live secrets.
    void setKeychainEnabled(bool enabled) { keychainEnabled_ = enabled; }
    bool keychainEnabled() const { return keychainEnabled_; }
    bool keychainAvailable() const;

    // Reads every provider on a worker thread and primes the cache, so the
    // first request served after this returns costs no keychain calls at all.
    //
    // This exists because reading the keychain means running a helper program,
    // and the key page's state endpoint asks for all ten providers at once. On
    // the GUI thread that is ten process spawns -- each with a multi-second
    // timeout and, on macOS, the possibility of raising its own authorisation
    // dialog -- so simply opening the page could freeze the window for seconds.
    // Call it when the server starts, which is before the browser is even sent
    // to the page. Returns immediately. Calling it again while a preload is
    // already running is a no-op rather than a second thread.
    void preloadAsync();

private:
    bool iniSave(const QString& providerId, const QString& key);
    StoredSecret iniLoad(const QString& providerId) const;
    bool iniRemove(const QString& providerId);

    bool keychainSave(const QString& providerId, const QString& key);
    bool keychainLoad(const QString& providerId, QString* out) const;
    bool keychainRemove(const QString& providerId);

    QString iniPath_;
    bool keychainEnabled_ = true;

    // Resolved secrets, kept so a request handler never has to reach for the
    // keychain on the GUI thread. Guarded by a mutex because preloadAsync()
    // fills it from a worker thread while the GUI thread reads it.
    //
    // An entry is present if and only if that provider has been resolved, so a
    // provider known to have no key is cached too -- without that it would be
    // re-probed on every request, which is the case that matters most here
    // because most providers have nothing stored.
    mutable QMutex cacheMutex_;
    mutable QHash<QString, StoredSecret> cache_;

    // The preload worker. Held and joined rather than detached, so it cannot
    // outlive the store it reads; cancelled cooperatively so the destructor does
    // not have to wait out the whole list.
    mutable std::atomic<bool> preloadCancelled_{false};
    mutable std::thread preloadThread_;

    // Serialises the INI layer against itself, which the cache mutex does not
    // cover: the cache guards the map, this guards the file underneath it.
    mutable QMutex iniMutex_;

    // The two persistent layers, and the read-only override layer, kept apart on
    // purpose: the override is consulted ahead of the cache and never stored in
    // it, so their caching rules cannot be conflated.
    StoredSecret environmentOverride(const QString& providerId) const;
    StoredSecret resolvePersistent(const QString& providerId) const;
    StoredSecret cachedOrLoad(const QString& providerId) const;
    // Both const because they only write members that are already mutable: a
    // const load() has to be able to fill the cache it just missed on.
    void cacheStore(const QString& providerId, const StoredSecret& secret) const;
    void cacheForget(const QString& providerId) const;
};

}  // namespace app
