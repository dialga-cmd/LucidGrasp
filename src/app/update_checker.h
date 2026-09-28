#pragma once

#include <QDateTime>
#include <QObject>
#include <QSettings>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace app {

// What we need out of a GitHub release. Deliberately small: the rest of the
// payload is ignored, so an added field upstream cannot change behaviour here.
struct ReleaseInfo {
    QString tag;    // as published, e.g. "v1.2.0"
    QString url;    // human-facing page for the release
    QString notes;  // release body, markdown
};

// How an attempt ended. Only UpdateAvailable is ever allowed to interrupt the
// user; see the comment on UpdateChecker::finished.
enum class CheckOutcome {
    UpToDate,        // reached GitHub, newest release is not newer than this build
    UpdateAvailable, // a newer release exists and the user has not muted it
    Unreachable,     // network, rate limit, timeout, or an unreadable payload
    Suppressed       // the user muted this version, or automatic checks are off
};

// --- Pure helpers ----------------------------------------------------------
// No network and no settings, so the self-test can drive them directly with
// fixtures. Every one of them fails towards silence: an update checker that
// cannot understand what it read must say nothing, because the only way to be
// wrong here is to tell every user to update, forever.

// Strips a leading "v" or "V" and surrounding whitespace, so "v1.2.0",
// "V1.2.0" and " 1.2.0 " all compare as the same version.
QString normaliseVersion(const QString &tag);

// Semantic-version precedence. Returns false -- meaning "do not claim an
// update" -- for anything that does not parse on either side, which is what
// keeps a malformed tag from becoming a permanent false alarm.
bool isNewerVersion(const QString &candidateTag, const QString &currentVersion);

// Reads the /releases/latest payload. Returns false for truncated JSON, an
// HTML error page served with a 200, a missing or empty tag, a missing page
// URL, and anything flagged draft or prerelease. On false, `out` is untouched.
bool parseLatestRelease(const QByteArray &json, ReleaseInfo* out);

// --- Persisted preferences -------------------------------------------------

// The opt-out, the muted version, and the daily throttle. Wrapped in a class
// rather than used inline for two reasons: the keys get a single home, and the
// self-test can point QSettings at a scratch directory and exercise the real
// thing instead of a stand-in.
class UpdateSettings
{
public:
    UpdateSettings();

    // True when the user asked us to stop checking. Checked before anything
    // touches the network, so opting out really does mean no traffic at all.
    bool isDisabled() const;
    void setDisabled(bool disabled);

    // The one version the user said not to mention. Stored normalised and
    // per-tag, so muting 1.2.0 still lets them hear about 1.3.0 -- a blanket
    // mute is a trap, because it also silences the security fixes.
    QString ignoredVersion() const;
    void setIgnoredVersion(const QString& tag);

    QDateTime lastCheck() const;
    void setLastCheck(const QDateTime& when);

    // False when checks are off, or when one already ran inside the interval.
    // The throttle is on *attempts*, not successes, which is the whole reason a
    // rate-limited GitHub costs one try a day rather than one per launch.
    bool shouldCheckNow(int intervalHours = 24) const;

private:
    // Ini format on purpose, on every platform. The default on Windows is the
    // registry, which QSettings::setPath cannot redirect, so a self-test there
    // would write to the real user settings.
    QSettings store_;
};

// --- The network side ------------------------------------------------------

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    static QString currentVersion();
    static QString releasesPageUrl();

    // Honours the opt-out and the daily throttle. Called once per launch.
    void checkOnStartup();

    // Runs a check now, ignoring the throttle, because the user asked for it.
    // Still a no-op when a check is already in flight.
    void checkNow();

    bool isChecking() const;
    bool isDisabled() const;
    void setDisabled(bool disabled);

signals:
    // A newer release the user has not muted. The only signal the UI turns into
    // something visible.
    void updateAvailable(const QString& tag, const QString& url, const QString& notes);

    // Emitted after every attempt that actually reached the network. A
    // suppressed or throttled request emits nothing at all, because from the
    // outside it is indistinguishable from never having run.
    void finished(CheckOutcome outcome);

private:
    void run();
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* net_ = nullptr;
    UpdateSettings settings_;
    bool checking_ = false;
};

} // namespace app
