#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

namespace app {

class SecretStore;

// Serves the bring-your-own-key page on an OS-assigned loopback port and
// answers its JSON calls. Validation runs here, in C++, never in the page:
// the browser never has to be trusted with a key, and a key never has to cross
// into JavaScript beyond the box the user just typed it into.
//
// Threat model: anything on the machine (and any page in the browser) can
// reach 127.0.0.1. Three things keep that from being enough:
//   1. the server binds to loopback only and stops with the app;
//   2. every request must carry a random per-session token, and the browser
//      page gets it from the URL the app opened itself;
//   3. Host must be our loopback authority and Origin, when present, must
//      match it, which blocks a foreign page from driving this server.
class KeyEntryServer : public QObject {
    Q_OBJECT

public:
    explicit KeyEntryServer(SecretStore* store, QObject* parent = nullptr);
    ~KeyEntryServer() override;

    // Binds and starts listening. On false, *error describes why.
    bool start(QString* error = nullptr);
    void stop();
    bool isRunning() const { return server_ != nullptr; }

    quint16 port() const { return port_; }
    QString token() const { return QString::fromLatin1(token_); }
    // The URL to hand to the browser: loopback host, ephemeral port, token.
    QUrl url() const;

    // --- Pure helpers, so the self-test can drive them without a socket ----

    struct Request {
        QByteArray method;
        QByteArray path;
        QByteArray query;
        QHash<QByteArray, QByteArray> headers;  // lower-case names
        QByteArray body;
    };

    // Parses a complete request (headers + body already buffered). False means
    // malformed, not incomplete; the caller decides completeness from
    // Content-Length before calling.
    static bool parseRequest(const QByteArray& raw, Request* out,
                             QString* error);
    // Length-independent comparison, so a wrong guess costs the same as a wrong
    // near-miss.
    static bool constantTimeEquals(const QByteArray& a, const QByteArray& b);
    // True when `host` is "127.0.0.1" or "localhost", optionally ":port", and
    // the port, if present, is ours.
    static bool hostIsLoopback(const QByteArray& host, quint16 port);

signals:
    // One per completed Validate click, whether or not a browser is still
    // listening. The app uses it to refresh its own enabled/disabled state.
    void validationFinished(const QString& id, bool ok, const QString& message);
    // Emitted after Sync, i.e. after at least one key was written or removed.
    void keysChanged();

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void dispatch(QTcpSocket* socket, const Request& req);

    void sendResponse(QTcpSocket* socket, int status,
                      const QByteArray& contentType, const QByteArray& body);
    bool authorized(const Request& req) const;
    bool originAllowed(const QByteArray& origin) const;

    QByteArray stateJson() const;
    QByteArray pageHtml() const;

    void handleValidate(QTcpSocket* socket, const QByteArray& body);
    void handleSync(QTcpSocket* socket, const QByteArray& body);
    void handleRemove(QTcpSocket* socket, const QByteArray& body);
    void finishValidation(QTcpSocket* socket, const QString& id, bool ok,
                          const QString& message);

    QTcpServer* server_ = nullptr;
    QNetworkAccessManager* net_ = nullptr;
    SecretStore* store_ = nullptr;
    QByteArray token_;
    quint16 port_ = 0;
    QHash<QTcpSocket*, QByteArray> buffers_;
};

}  // namespace app
