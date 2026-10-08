#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace app {

struct ReleaseInfo {
    QString tag;
    QString url;
    QString notes;
};

enum class CheckOutcome {
    UpToDate,
    UpdateAvailable,
    Unreachable,
    Suppressed
};

QString normaliseVersion(const QString &tag);

bool isNewerVersion(const QString &candidateTag, const QString &currentVersion);

bool parseLatestRelease(const QByteArray &json, ReleaseInfo* out);

class UpdateSettings
{
public:
    UpdateSettings();

    bool isDisabled() const;
    void setDisabled(bool disabled);

    QString ignoredVersion() const;
    void setIgnoredVersion(const QString& tag);

private:
    QSettings store_;
};

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    static QString currentVersion();

    void checkOnStartup();

    void checkNow();

    bool isChecking() const;
    bool isDisabled() const;
    void setDisabled(bool disabled);
    void setIgnoredVersion(const QString& tag);

signals:
    void updateAvailable(const QString& tag, const QString& url, const QString& notes);

    void finished(CheckOutcome outcome);

private:
    void run();
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* net_ = nullptr;
    UpdateSettings settings_;
    bool checking_ = false;
};

}
