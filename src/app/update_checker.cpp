#include "app/update_checker.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

// Both come from CMake, so there is one place to bump a release. The fallbacks
// only matter for a translation unit built outside the project's build system.
#ifndef LUCIDGRASP_VERSION
#define LUCIDGRASP_VERSION "0.0.0"
#endif
#ifndef LUCIDGRASP_REPO
#define LUCIDGRASP_REPO "dialga-cmd/LucidGrasp"
#endif

namespace {

// Long enough for a slow link, short enough that a black-holed connection does
// not leave the checker busy. The failure is silent, so the cost of being
// generous is one wasted second, and the cost of being stingy is a missed
// update notice.
constexpr int kTimeoutMs = 10000;

struct ParsedVersion {
    bool valid = false;
    qint64 part[3] = {0, 0, 0};
    QString pre;  // prerelease label, without the leading '-'
};

// Semantic version, with two strictnesses on top of a plain numeric split.
//
//  - A leading zero in any component is rejected. This is a real semver rule,
//    and it is the rule that saves us from a date-based tag: "v2024.01.15"
//    parses numerically as version 2024.1.15, which would outrank 1.1.0 and
//    tell every user to update, on every launch, forever.
//  - A component that will not fit in a qint64 is rejected rather than
//    wrapping, so a long numeric tag cannot come out as a small one.
ParsedVersion parseVersion(const QString& raw)
{
    ParsedVersion out;

    const QString v = app::normaliseVersion(raw);
    if (v.isEmpty())
        return out;

    QString core = v;
    const int dash = core.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        out.pre = core.mid(dash + 1);
        core = core.left(dash);
    }
    // Build metadata is explicitly not part of precedence, so it is dropped.
    const int plus = core.indexOf(QLatin1Char('+'));
    if (plus >= 0)
        core = core.left(plus);

    const QStringList parts = core.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size() > 3)
        return out;

    for (int i = 0; i < parts.size(); ++i) {
        const QString& p = parts.at(i);
        if (p.isEmpty())
            return out;
        if (p.size() > 1 && p.startsWith(QLatin1Char('0')))
            return out;  // leading zero
        for (const QChar& ch : p) {
            if (!ch.isDigit())
                return out;
        }
        bool ok = false;
        out.part[i] = p.toLongLong(&ok);
        if (!ok)
            return out;
    }

    out.valid = true;
    return out;
}

}  // namespace

namespace app {

QString normaliseVersion(const QString& tag)
{
    QString v = tag.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v.remove(0, 1);
    return v.trimmed();
}

bool isNewerVersion(const QString& candidateTag, const QString& currentVersion)
{
    const ParsedVersion a = parseVersion(candidateTag);
    const ParsedVersion b = parseVersion(currentVersion);

    // Either side unreadable means we do not know which is newer, and the only
    // safe answer is to claim nothing.
    if (!a.valid || !b.valid)
        return false;

    for (int i = 0; i < 3; ++i) {
        if (a.part[i] != b.part[i])
            return a.part[i] > b.part[i];
    }

    // Identical numbers. A release outranks its own prereleases, so the side
    // without a prerelease label is the newer one. Two prereleases of the same
    // version (rc1 against rc2) are left alone: ordering those needs the full
    // semver identifier comparison, and guessing would only ever nag.
    if (a.pre.isEmpty() != b.pre.isEmpty())
        return a.pre.isEmpty();

    return false;
}

bool parseLatestRelease(const QByteArray& json, ReleaseInfo* out)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject o = doc.object();

    // /releases/latest already excludes drafts and prereleases, but the endpoint
    // is not the only thing feeding this parser -- the self-test is -- and a
    // defensive check here costs nothing against a very bad failure mode.
    if (o.value(QLatin1String("draft")).toBool(false))
        return false;
    if (o.value(QLatin1String("prerelease")).toBool(false))
        return false;

    const QString tag = o.value(QLatin1String("tag_name")).toString().trimmed();
    if (tag.isEmpty())
        return false;

    const QString url = o.value(QLatin1String("html_url")).toString().trimmed();
    // Without a page to send the user to there is nothing actionable to show,
    // so this is treated as no release rather than as a broken dialog.
    if (url.isEmpty())
        return false;

    if (out) {
        ReleaseInfo info;
        info.tag = tag;
        info.url = url;
        info.notes = o.value(QLatin1String("body")).toString().trimmed();
        *out = info;
    }
    return true;
}

// --- UpdateSettings --------------------------------------------------------

UpdateSettings::UpdateSettings()
    : store_(QSettings::IniFormat,
             QSettings::UserScope,
             QCoreApplication::organizationName(),
             QCoreApplication::applicationName())
{
}

bool UpdateSettings::isDisabled() const
{
    return store_.value(QStringLiteral("updates/disabled"), false).toBool();
}

void UpdateSettings::setDisabled(bool disabled)
{
    store_.setValue(QStringLiteral("updates/disabled"), disabled);
}

QString UpdateSettings::ignoredVersion() const
{
    return store_.value(QStringLiteral("updates/ignoredVersion")).toString();
}

void UpdateSettings::setIgnoredVersion(const QString& tag)
{
    store_.setValue(QStringLiteral("updates/ignoredVersion"),
                    normaliseVersion(tag));
}

QDateTime UpdateSettings::lastCheck() const
{
    return store_.value(QStringLiteral("updates/lastCheck")).toDateTime();
}

void UpdateSettings::setLastCheck(const QDateTime& when)
{
    store_.setValue(QStringLiteral("updates/lastCheck"), when);
}

bool UpdateSettings::shouldCheckNow(int intervalHours) const
{
    if (isDisabled())
        return false;

    const QDateTime last = lastCheck();
    if (!last.isValid())
        return true;  // never checked

    return QDateTime::currentDateTimeUtc()
        >= last.addSecs(qint64(intervalHours) * 3600);
}

// --- UpdateChecker ---------------------------------------------------------

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , net_(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion()
{
    return QCoreApplication::applicationVersion();
}

QString UpdateChecker::releasesPageUrl()
{
    return QStringLiteral("https://github.com/" LUCIDGRASP_REPO "/releases");
}

void UpdateChecker::checkOnStartup()
{
    if (settings_.shouldCheckNow())
        run();
}

void UpdateChecker::checkNow()
{
    run();
}

bool UpdateChecker::isChecking() const
{
    return checking_;
}

bool UpdateChecker::isDisabled() const
{
    return settings_.isDisabled();
}

void UpdateChecker::setDisabled(bool disabled)
{
    settings_.setDisabled(disabled);
}

void UpdateChecker::run()
{
    if (checking_)
        return;  // a manual click during an automatic check is not an error
    checking_ = true;

    const QUrl url(QStringLiteral("https://api.github.com/repos/" LUCIDGRASP_REPO
                                  "/releases/latest"));

    QNetworkRequest request(url);
    // GitHub rejects requests without a User-Agent outright, so this is not
    // optional polish.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LucidGrasp/" LUCIDGRASP_VERSION));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");

    // Follow a renamed repository, but never a redirect that drops off https.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // Covers the common stall. The timer below covers the rest, because
    // "still running forever" must never leave the checker permanently busy.
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply* reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { handleReply(reply); });

    // QPointer, not a bare pointer: a reply that finished first is deleted, and
    // touching it afterwards would be undefined behaviour rather than a no-op.
    const QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(kTimeoutMs + 2000, this, [guard] {
        if (guard && guard->isRunning())
            guard->abort();
    });
}

void UpdateChecker::handleReply(QNetworkReply* reply)
{
    reply->deleteLater();

    // The attempt is recorded whatever happened. That is the anti-hammering
    // mechanism, not a detail: unauthenticated GitHub allows 60 requests per
    // hour per IP address, and an IP is shared by everyone behind one NAT or
    // carrier-grade NAT. Recording failures too means an office that has
    // exhausted the shared budget retries once a day rather than once per
    // launch. It also makes a failure indistinguishable from a check that never
    // happened, which is exactly what we want to show the user: nothing.
    settings_.setLastCheck(QDateTime::currentDateTimeUtc());

    if (reply->error() != QNetworkReply::NoError) {
        checking_ = false;
        emit finished(CheckOutcome::Unreachable);
        return;
    }

    ReleaseInfo info;
    if (!parseLatestRelease(reply->readAll(), &info)) {
        checking_ = false;
        emit finished(CheckOutcome::Unreachable);
        return;
    }

    if (!isNewerVersion(info.tag, currentVersion())) {
        checking_ = false;
        emit finished(CheckOutcome::UpToDate);
        return;
    }

    if (settings_.ignoredVersion() == normaliseVersion(info.tag)) {
        checking_ = false;
        emit finished(CheckOutcome::Suppressed);
        return;
    }

    checking_ = false;
    // Notification first, outcome second. `finished` has to mean "the check is
    // over and everything that came of it has happened" -- emitted the other way
    // round, a slot connected to it would see an available update that has not
    // been shown to anyone yet.
    emit updateAvailable(info.tag, info.url, info.notes);
    emit finished(CheckOutcome::UpdateAvailable);
}

}  // namespace app
