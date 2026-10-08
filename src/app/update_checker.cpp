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

#ifndef LUCIDGRASP_VERSION
#define LUCIDGRASP_VERSION "0.0.0"
#endif
#ifndef LUCIDGRASP_REPO
#define LUCIDGRASP_REPO "dialga-cmd/LucidGrasp"
#endif

namespace {

constexpr int kTimeoutMs = 10000;

struct ParsedVersion {
    bool valid = false;
    qint64 part[3] = {0, 0, 0};
    QString pre;
};

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
            return out;
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

}

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

    if (!a.valid || !b.valid)
        return false;

    for (int i = 0; i < 3; ++i) {
        if (a.part[i] != b.part[i])
            return a.part[i] > b.part[i];
    }

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

    if (o.value(QLatin1String("draft")).toBool(false))
        return false;
    if (o.value(QLatin1String("prerelease")).toBool(false))
        return false;

    const QString tag = o.value(QLatin1String("tag_name")).toString().trimmed();
    if (tag.isEmpty())
        return false;

    const QString url = o.value(QLatin1String("html_url")).toString().trimmed();
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
        return true;

    return QDateTime::currentDateTimeUtc()
        >= last.addSecs(qint64(intervalHours) * 3600);
}

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

void UpdateChecker::setIgnoredVersion(const QString& tag)
{
    settings_.setIgnoredVersion(tag);
}

void UpdateChecker::run()
{
    if (checking_)
        return;
    checking_ = true;

    const QUrl url(QStringLiteral("https://api.github.com/repos/" LUCIDGRASP_REPO
                                  "/releases/latest"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LucidGrasp/" LUCIDGRASP_VERSION));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");

    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply* reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { handleReply(reply); });

    const QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(kTimeoutMs + 2000, this, [guard] {
        if (guard && guard->isRunning())
            guard->abort();
    });
}

void UpdateChecker::handleReply(QNetworkReply* reply)
{
    reply->deleteLater();

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
    emit updateAvailable(info.tag, info.url, info.notes);
    emit finished(CheckOutcome::UpdateAvailable);
}

}
