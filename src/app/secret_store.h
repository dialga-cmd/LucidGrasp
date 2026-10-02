#pragma once

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

private:
    bool iniSave(const QString& providerId, const QString& key);
    StoredSecret iniLoad(const QString& providerId) const;
    bool iniRemove(const QString& providerId);

    bool keychainSave(const QString& providerId, const QString& key);
    bool keychainLoad(const QString& providerId, QString* out) const;
    bool keychainRemove(const QString& providerId);

    QString iniPath_;
    bool keychainEnabled_ = true;
};

}  // namespace app
