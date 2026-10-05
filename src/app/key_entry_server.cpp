#include "app/key_entry_server.h"

#include "app/key_entry_page.h"
#include "app/providers.h"
#include "app/secret_store.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace {

// A request body larger than this is not our page talking. Keys and tiny JSON
// objects are all we ever receive.
constexpr int kMaxBody = 64 * 1024;
constexpr int kTimeoutMs = 20000;

// How long a connection may sit without completing a request. Generous enough
// that a page on a slow link is never cut off mid-load, short enough that an
// abandoned connection does not outlive the session.
constexpr int kIdleTimeoutMs = 30000;

QByteArray randomToken()
{
    static const char kHex[] = "0123456789abcdef";
    QByteArray token(32, '\0');
    QRandomGenerator* rng = QRandomGenerator::system();
    for (int i = 0; i < token.size(); ++i)
        token[i] = kHex[rng->bounded(16)];
    return token;
}

QString backendLabel(app::SecretBackend backend)
{
    switch (backend) {
    case app::SecretBackend::Environment:
        return QStringLiteral("environment override");
    case app::SecretBackend::Keychain:
        return QStringLiteral("OS keychain");
    case app::SecretBackend::Settings:
        return QStringLiteral("settings file");
    case app::SecretBackend::None:
        break;
    }
    return QStringLiteral("not stored");
}

QByteArray jsonBytes(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray errorJson(const QString& message)
{
    return jsonBytes(QJsonObject{{QStringLiteral("ok"), false},
                                 {QStringLiteral("message"), message}});
}

// A provider probe: one request that answers "is this key usable?".
struct Probe {
    QByteArray method = QByteArrayLiteral("GET");
    QUrl url;
    QByteArray body;
    QByteArray contentType;
    QVector<QPair<QByteArray, QByteArray>> headers;
};

// One-pixel transparent PNG, used only to make the Vision request well-formed.
// A LABEL_DETECTION call is one billable unit, which is the cost of admitting a
// GCP key; there is no free "is this key real" endpoint.
const QByteArray kOnePixelPng = QByteArrayLiteral(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
    "+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==");

bool buildProbe(const app::Provider& provider, const QString& key, Probe* out,
                QString* error)
{
    const QString& v = provider.validator;

    if (v == QLatin1String("trace_moe")) {
        // /search is POST-only (a GET returns 405), so reachability is probed
        // with /me, which answers 200 anonymously and reports the quota. A
        // donor key, when the user supplies one, travels in the x-trace-key
        // header exactly as trace.moe expects; an invalid key there is a 403.
        out->method = QByteArrayLiteral("GET");
        out->url = QUrl(QStringLiteral("https://api.trace.moe/me"));
        if (app::SecretStore::hasValue(key))
            out->headers.append({QByteArrayLiteral("x-trace-key"), key.toUtf8()});
        return true;
    }

    if (key.isEmpty()) {
        if (error)
            *error = QStringLiteral("Enter a key first.");
        return false;
    }

    if (v == QLatin1String("google_vision")) {
        const QJsonObject image{{QStringLiteral("content"),
                                 QString::fromLatin1(kOnePixelPng)}};
        const QJsonObject feature{{QStringLiteral("type"),
                                   QStringLiteral("LABEL_DETECTION")},
                                  {QStringLiteral("maxResults"), 1}};
        const QJsonArray features{feature};
        const QJsonObject one{{QStringLiteral("image"), image},
                              {QStringLiteral("features"), features}};
        const QJsonArray requests{one};
        const QJsonObject root{{QStringLiteral("requests"), requests}};

        QUrl url(QStringLiteral("https://vision.googleapis.com/v1/images:annotate"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), key);
        url.setQuery(q);
        out->method = QByteArrayLiteral("POST");
        out->url = url;
        out->body = jsonBytes(root);
        out->contentType = QByteArrayLiteral("application/json");
        return true;
    }

    if (v == QLatin1String("saucenao")) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), key);
        q.addQueryItem(QStringLiteral("output_type"), QStringLiteral("2"));
        q.addQueryItem(QStringLiteral("db"), QStringLiteral("999"));
        q.addQueryItem(QStringLiteral("url"),
                       QStringLiteral("https://saucenao.com/images/static/logo.png"));
        out->method = QByteArrayLiteral("POST");
        out->url = QUrl(QStringLiteral("https://saucenao.com/search.php"));
        out->body = q.query(QUrl::FullyEncoded).toUtf8();
        out->contentType = QByteArrayLiteral("application/x-www-form-urlencoded");
        return true;
    }

    if (v == QLatin1String("serpapi")) {
        QUrl url(QStringLiteral("https://serpapi.com/account"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), key);
        url.setQuery(q);
        out->url = url;
        return true;
    }

    if (v == QLatin1String("zenserp")) {
        QUrl url(QStringLiteral("https://app.zenserp.com/api/v2/status"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("apikey"), key);
        url.setQuery(q);
        out->url = url;
        return true;
    }

    if (v == QLatin1String("hasdata")) {
        QUrl url(QStringLiteral("https://api.hasdata.com/scrape/google/images"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), QStringLiteral("test"));
        q.addQueryItem(QStringLiteral("num"), QStringLiteral("1"));
        url.setQuery(q);
        out->url = url;
        out->headers.append({QByteArrayLiteral("x-api-key"), key.toUtf8()});
        return true;
    }

    if (v == QLatin1String("scraperapi")) {
        QUrl url(QStringLiteral("https://api.scraperapi.com/account"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), key);
        url.setQuery(q);
        out->url = url;
        return true;
    }

    if (v == QLatin1String("scrape_do")) {
        QUrl url(QStringLiteral("https://api.scrape.do/info"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("token"), key);
        url.setQuery(q);
        out->url = url;
        return true;
    }

    if (v == QLatin1String("zenrows")) {
        QUrl url(QStringLiteral("https://api.zenrows.com/v1/"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("apikey"), key);
        q.addQueryItem(QStringLiteral("url"),
                       QStringLiteral("https://example.com"));
        url.setQuery(q);
        out->url = url;
        return true;
    }

    if (v == QLatin1String("outscraper")) {
        out->url = QUrl(QStringLiteral("https://api.app.outscraper.com/profile"));
        out->headers.append({QByteArrayLiteral("X-API-KEY"), key.toUtf8()});
        return true;
    }

    if (v == QLatin1String("serpstack")) {
        QUrl url(QStringLiteral("https://api.serpstack.com/account"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("access_key"), key);
        url.setQuery(q);
        out->url = url;
        return true;
    }

    if (error)
        *error = QStringLiteral("This provider cannot be validated automatically.");
    return false;
}

void judgeProbe(const app::Provider& provider, int status,
                const QByteArray& body, const QString& netError, bool withKey,
                bool* ok, QString* message)
{
    const QString& v = provider.validator;

    if (v == QLatin1String("trace_moe")) {
        if (status >= 200 && status < 300) {
            *ok = true;
            *message = withKey
                           ? QStringLiteral("Key accepted — trace.moe is reachable.")
                           : QStringLiteral("trace.moe is reachable (no key needed).");
        } else if (status == 429) {
            *ok = true;
            *message = QStringLiteral("trace.moe is reachable (rate limited).");
        } else if (status == 401 || status == 403) {
            *ok = false;
            *message = QStringLiteral("trace.moe rejected the key (HTTP %1).")
                           .arg(status);
        } else {
            *ok = false;
            *message = QStringLiteral("Could not reach trace.moe (HTTP %1).")
                           .arg(status);
        }
        return;
    }

    if (v == QLatin1String("google_vision")) {
        if (status == 200) {
            *ok = true;
            *message = QStringLiteral("Key accepted — Web Detection is reachable.");
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        const QString detail = root.value(QStringLiteral("error"))
                                   .toObject()
                                   .value(QStringLiteral("message"))
                                   .toString();
        *ok = false;
        *message = detail.isEmpty()
                       ? QStringLiteral("Google rejected the key (HTTP %1).")
                             .arg(status)
                       : detail;
        return;
    }

    if (v == QLatin1String("saucenao")) {
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        const QJsonObject header = root.value(QStringLiteral("header")).toObject();
        if (root.isEmpty()) {
            *ok = false;
            *message = QStringLiteral("Unexpected SauceNAO response (HTTP %1).")
                           .arg(status);
            return;
        }
        const int headerStatus = header.value(QStringLiteral("status")).toInt(0);
        if (headerStatus < 0) {
            const QString detail =
                header.value(QStringLiteral("message")).toString();
            *ok = false;
            *message = detail.isEmpty()
                           ? QStringLiteral("SauceNAO rejected the key.")
                           : detail;
            return;
        }
        *ok = true;
        *message = QStringLiteral("Key accepted.");
        return;
    }

    // Everything else parks an account/profile endpoint around a 2xx for a
    // valid key and 401/403 for a rejected one.
    if (status >= 200 && status < 300) {
        *ok = true;
        *message = QStringLiteral("Key accepted.");
    } else if (status == 401 || status == 403) {
        *ok = false;
        *message = QStringLiteral("Key rejected (HTTP %1).").arg(status);
    } else if (status == 0) {
        *ok = false;
        *message = netError.isEmpty()
                       ? QStringLiteral("No response from the provider.")
                       : netError;
    } else {
        *ok = false;
        *message = QStringLiteral("Could not verify (HTTP %1).").arg(status);
    }
}

}  // namespace

namespace app {

KeyEntryServer::KeyEntryServer(SecretStore* store, QObject* parent)
    : QObject(parent)
    , net_(new QNetworkAccessManager(this))
    , store_(store)
{
}

KeyEntryServer::~KeyEntryServer() = default;

bool KeyEntryServer::start(QString* error)
{
    if (server_)
        return true;

    server_ = new QTcpServer(this);
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        if (error)
            *error = server_->errorString();
        server_->deleteLater();
        server_ = nullptr;
        return false;
    }

    port_ = server_->serverPort();
    token_ = randomToken();
    connect(server_, &QTcpServer::newConnection, this,
            &KeyEntryServer::onNewConnection);

    // Warm the key store now, while nothing is waiting on it. The browser is
    // opened after this returns and still has to load the page, so by the time
    // the first request arrives the cache is normally already primed and the
    // handler runs no helper programs at all.
    store_->preloadAsync();
    return true;
}

void KeyEntryServer::stop()
{
    if (!server_)
        return;
    // Close every live socket before dropping the bookkeeping. The timers are
    // children of their sockets, so aborting is also what releases them; doing
    // it explicitly first means nothing can fire against a half-cleared map.
    const QList<QTcpSocket*> sockets = idleTimers_.keys();
    for (QTcpSocket* socket : sockets)
        socket->abort();

    server_->close();
    server_->deleteLater();
    server_ = nullptr;
    token_.clear();
    port_ = 0;
    buffers_.clear();
    idleTimers_.clear();
}

QUrl KeyEntryServer::url() const
{
    QUrl u;
    u.setScheme(QStringLiteral("http"));
    u.setHost(QStringLiteral("127.0.0.1"));
    u.setPort(port_);
    u.setPath(QStringLiteral("/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("token"), QString::fromLatin1(token_));
    u.setQuery(q);
    return u;
}

// --- Connection handling ---------------------------------------------------

void KeyEntryServer::armIdleTimer(QTcpSocket* socket)
{
    if (!socket)
        return;

    QTimer* timer = idleTimers_.value(socket, nullptr);
    if (!timer) {
        timer = new QTimer(socket);  // child: dies with the socket
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, socket] {
            // Silent close. The client either went away or is not our page, and
            // either way there is nothing useful to say to it.
            socket->abort();
        });
        idleTimers_.insert(socket, timer);
    }
    timer->start(kIdleTimeoutMs);
}

void KeyEntryServer::onNewConnection()
{
    while (server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        buffers_.insert(socket, QByteArray());
        armIdleTimer(socket);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            buffers_.remove(socket);
            idleTimers_.remove(socket);  // the timer is a child; this drops the ref
            socket->deleteLater();
        });
    }
}

void KeyEntryServer::onReadyRead(QTcpSocket* socket)
{
    QByteArray& buffer = buffers_[socket];
    buffer.append(socket->readAll());

    // Any progress at all buys another window, so a slow but legitimate upload
    // is not cut off, and an early return below still leaves the socket bounded.
    armIdleTimer(socket);

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        // Headers alone are already over the limit, so there is no length worth
        // waiting for. The reply closes the connection via sendResponse; the
        // buffer is dropped too so a half-received oversized request cannot be
        // reconsidered on the next read.
        if (buffer.size() > kMaxBody) {
            buffer.clear();
            sendResponse(socket, 413, QByteArrayLiteral("text/plain"),
                         QByteArrayLiteral("request too large"));
        }
        return;
    }

    int contentLength = 0;
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        if (line.left(colon).trimmed().toLower() == QByteArrayLiteral("content-length"))
            contentLength = line.mid(colon + 1).trimmed().toInt();
    }

    if (contentLength < 0 || contentLength > kMaxBody) {
        // Oversized or malformed Content-Length: the request is refused on its
        // declared size, not on what actually turned up, and the buffer goes
        // with it so the remainder cannot be reinterpreted.
        buffer.clear();
        sendResponse(socket, 413, QByteArrayLiteral("text/plain"),
                     QByteArrayLiteral("request too large"));
        return;
    }

    const int total = headerEnd + 4 + contentLength;
    if (buffer.size() < total)
        return;  // body still arriving

    const QByteArray raw = buffer.left(total);
    buffer.clear();

    Request req;
    QString parseError;
    if (!parseRequest(raw, &req, &parseError)) {
        sendResponse(socket, 400, QByteArrayLiteral("text/plain"),
                     parseError.toUtf8());
        return;
    }
    dispatch(socket, req);
}

void KeyEntryServer::dispatch(QTcpSocket* socket, const Request& req)
{
    // Host must be our loopback authority. A browser cannot be tricked into
    // setting it, so this is the cheap first gate.
    if (!hostIsLoopback(req.headers.value("host"), port_)) {
        sendResponse(socket, 403, QByteArrayLiteral("text/plain"),
                     QByteArrayLiteral("forbidden: bad host"));
        return;
    }
    // Origin is sent on cross-origin and (for POST) same-origin fetches. When
    // present it must be us; a foreign page aiming at 127.0.0.1 is refused.
    const QByteArray origin = req.headers.value("origin");
    if (!origin.isEmpty() && !originAllowed(origin)) {
        sendResponse(socket, 403, QByteArrayLiteral("text/plain"),
                     QByteArrayLiteral("forbidden: bad origin"));
        return;
    }

    const bool isPage = req.method == QByteArrayLiteral("GET")
                        && (req.path == QByteArrayLiteral("/")
                            || req.path == QByteArrayLiteral("/index.html"));
    if (isPage) {
        if (!authorized(req)) {
            sendResponse(socket, 403, QByteArrayLiteral("text/plain"),
                         QByteArrayLiteral("forbidden: bad token"));
            return;
        }
        sendResponse(socket, 200,
                     QByteArrayLiteral("text/html; charset=utf-8"),
                     pageHtml());
        return;
    }

    if (!req.path.startsWith(QByteArrayLiteral("/api/"))) {
        sendResponse(socket, 404, QByteArrayLiteral("text/plain"),
                     QByteArrayLiteral("not found"));
        return;
    }

    if (!authorized(req)) {
        sendResponse(socket, 403,
                     QByteArrayLiteral("application/json"),
                     errorJson(QStringLiteral("Bad or missing token.")));
        return;
    }

    if (req.method == QByteArrayLiteral("GET")
        && req.path == QByteArrayLiteral("/api/state")) {
        sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                     stateJson());
        return;
    }
    if (req.method == QByteArrayLiteral("POST")
        && req.path == QByteArrayLiteral("/api/validate")) {
        handleValidate(socket, req.body);
        return;
    }
    if (req.method == QByteArrayLiteral("POST")
        && req.path == QByteArrayLiteral("/api/sync")) {
        handleSync(socket, req.body);
        return;
    }
    if (req.method == QByteArrayLiteral("POST")
        && req.path == QByteArrayLiteral("/api/remove")) {
        handleRemove(socket, req.body);
        return;
    }

    sendResponse(socket, 404, QByteArrayLiteral("application/json"),
                 errorJson(QStringLiteral("Unknown endpoint.")));
}

void KeyEntryServer::sendResponse(QTcpSocket* socket, int status,
                                  const QByteArray& contentType,
                                  const QByteArray& body)
{
    if (!socket)
        return;

    QByteArray reason;
    switch (status) {
    case 200:
        reason = QByteArrayLiteral("OK");
        break;
    case 400:
        reason = QByteArrayLiteral("Bad Request");
        break;
    case 403:
        reason = QByteArrayLiteral("Forbidden");
        break;
    case 404:
        reason = QByteArrayLiteral("Not Found");
        break;
    case 413:
        reason = QByteArrayLiteral("Payload Too Large");
        break;
    default:
        reason = QByteArrayLiteral("Error");
        break;
    }

    QByteArray head;
    head += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    head += "Content-Type: " + contentType + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    head += "Referrer-Policy: no-referrer\r\n";
    head += "Content-Security-Policy: default-src 'none'; "
            "style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
            "connect-src 'self'; img-src 'self' data:; base-uri 'none'; "
            "form-action 'none'\r\n";
    head += "Connection: close\r\n\r\n";

    socket->write(head);
    socket->write(body);
    socket->disconnectFromHost();
}

bool KeyEntryServer::authorized(const Request& req) const
{
    if (token_.isEmpty())
        return false;

    QByteArray supplied = req.headers.value("x-lucidgrasp-token");
    if (supplied.isEmpty()) {
        const QUrlQuery query(QString::fromLatin1(req.query));
        supplied = query.queryItemValue(QStringLiteral("token")).toLatin1();
    }
    return constantTimeEquals(supplied, token_);
}

bool KeyEntryServer::originAllowed(const QByteArray& origin) const
{
    const QByteArray expectedV4 =
        "http://127.0.0.1:" + QByteArray::number(port_);
    const QByteArray expectedName =
        "http://localhost:" + QByteArray::number(port_);
    return origin == expectedV4 || origin == expectedName;
}

// --- API handlers ----------------------------------------------------------

void KeyEntryServer::handleValidate(QTcpSocket* socket, const QByteArray& body)
{
    const QJsonObject input = QJsonDocument::fromJson(body).object();
    const QString id = input.value(QStringLiteral("id")).toString();
    QString key = input.value(QStringLiteral("key")).toString().trimmed();

    const Provider* provider = providerById(id);
    if (!provider) {
        finishValidation(socket, id, false, QStringLiteral("Unknown provider."));
        return;
    }

    // A stored key can be re-validated without re-typing it: when the page
    // sends nothing, fall back to what is already saved. This also lets an
    // optional key (trace.moe) be re-checked by pressing the button again.
    if (key.isEmpty()) {
        const StoredSecret stored = store_->load(id);
        if (stored.found())
            key = stored.value;
    }
    if (provider->needsKey && !SecretStore::hasValue(key)) {
        finishValidation(socket, id, false, QStringLiteral("Enter a key first."));
        return;
    }
    const bool withKey = SecretStore::hasValue(key);

    Probe probe;
    QString buildError;
    if (!buildProbe(*provider, key, &probe, &buildError)) {
        finishValidation(socket, id, false,
                         buildError.isEmpty()
                             ? QStringLiteral("Cannot validate this provider.")
                             : buildError);
        return;
    }

    QNetworkRequest request(probe.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LucidGrasp/")
                          + QCoreApplication::applicationVersion());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);
    if (!probe.contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QString::fromLatin1(probe.contentType));
    for (const auto& header : probe.headers)
        request.setRawHeader(header.first, header.second);

    QNetworkReply* reply =
        probe.method == QByteArrayLiteral("POST")
            ? net_->post(request, probe.body)
            : net_->get(request);

    const QString providerId = id;
    const QPointer<QTcpSocket> guard(socket);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, guard, providerId, provider, withKey] {
                reply->deleteLater();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                        .toInt();
                const QByteArray payload = reply->readAll();

                bool ok = false;
                QString message;
                judgeProbe(*provider, status, payload, reply->errorString(),
                           withKey, &ok, &message);
                finishValidation(guard, providerId, ok, message);
            });
}

void KeyEntryServer::finishValidation(QTcpSocket* socket, const QString& id,
                                      bool ok, const QString& message)
{
    if (socket) {
        sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                     jsonBytes(QJsonObject{{QStringLiteral("ok"), ok},
                                           {QStringLiteral("message"), message}}));
    }
    emit validationFinished(id, ok, message);
}

void KeyEntryServer::handleSync(QTcpSocket* socket, const QByteArray& body)
{
    const QJsonObject input = QJsonDocument::fromJson(body).object();
    const QJsonObject keys = input.value(QStringLiteral("keys")).toObject();

    QJsonArray saved;
    QJsonArray failed;
    bool changed = false;

    for (auto it = keys.begin(); it != keys.end(); ++it) {
        const QString id = it.key();
        const QString key = it.value().toString().trimmed();
        if (!SecretStore::hasValue(key))
            continue;

        const Provider* provider = providerById(id);
        if (!provider) {
            failed.append(QJsonObject{{QStringLiteral("id"), id},
                                      {QStringLiteral("message"),
                                       QStringLiteral("Unknown provider.")}});
            continue;
        }
        // A key is stored for every known provider, including the keyless one
        // (trace.moe), so a user who has a donor key can supply it for a
        // higher quota even though the provider works without one.
        if (store_->save(id, key)) {
            saved.append(id);
            changed = true;
        } else {
            failed.append(QJsonObject{
                {QStringLiteral("id"), id},
                {QStringLiteral("message"),
                 QStringLiteral("Could not store the key.")}});
        }
    }

    const QJsonArray removals = input.value(QStringLiteral("remove")).toArray();
    for (const QJsonValue& value : removals) {
        const QString id = value.toString();
        if (!providerById(id))
            continue;
        store_->remove(id);
        changed = true;
    }

    if (changed)
        emit keysChanged();

    sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                 jsonBytes(QJsonObject{{QStringLiteral("ok"), failed.isEmpty()},
                                       {QStringLiteral("saved"), saved},
                                       {QStringLiteral("failed"), failed}}));
}

void KeyEntryServer::handleRemove(QTcpSocket* socket, const QByteArray& body)
{
    const QJsonObject input = QJsonDocument::fromJson(body).object();
    const QString id = input.value(QStringLiteral("id")).toString();
    if (!providerById(id)) {
        sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                     errorJson(QStringLiteral("Unknown provider.")));
        return;
    }
    store_->remove(id);
    emit keysChanged();
    sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                 jsonBytes(QJsonObject{{QStringLiteral("ok"), true}}));
}

QByteArray KeyEntryServer::pageHtml() const
{
    return QByteArray(keyEntryPageHtml());
}

QByteArray KeyEntryServer::stateJson() const
{
    QJsonArray array;
    bool searchEnabled = false;

    for (const Provider& provider : providers()) {
        const StoredSecret secret = store_->load(provider.id);
        const bool hasKey = secret.found();
        const bool enabled = !provider.needsKey || hasKey;
        // The global switch is about real, key-based providers. A keyless
        // provider (trace.moe) is always enabled but cannot, on its own, make
        // the general surface scan possible.
        if (provider.needsKey && hasKey)
            searchEnabled = true;

        QJsonArray steps;
        for (const QString& step : provider.steps)
            steps.append(step);

        array.append(QJsonObject{
            {QStringLiteral("id"), provider.id},
            {QStringLiteral("name"), provider.name},
            {QStringLiteral("shortName"), provider.shortName},
            {QStringLiteral("category"), provider.category},
            {QStringLiteral("description"), provider.description},
            {QStringLiteral("signupUrl"), provider.signupUrl},
            {QStringLiteral("needsKey"), provider.needsKey},
            {QStringLiteral("hasKey"), hasKey},
            {QStringLiteral("masked"), hasKey ? SecretStore::mask(secret.value)
                                              : QString()},
            {QStringLiteral("source"), backendLabel(secret.backend)},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("steps"), steps},
        });
    }

    return jsonBytes(QJsonObject{{QStringLiteral("ok"), true},
                                 {QStringLiteral("searchEnabled"),
                                  searchEnabled},
                                 {QStringLiteral("providers"), array}});
}

// --- Pure helpers ----------------------------------------------------------

bool KeyEntryServer::parseRequest(const QByteArray& raw, Request* out,
                                  QString* error)
{
    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (error)
            *error = QStringLiteral("missing header terminator");
        return false;
    }

    const QList<QByteArray> lines = raw.left(headerEnd).split('\n');
    if (lines.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty request");
        return false;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        if (error)
            *error = QStringLiteral("malformed request line");
        return false;
    }

    Request request;
    request.method = requestLine.at(0).trimmed().toUpper();

    QByteArray target = requestLine.at(1).trimmed();
    // Absolute-form targets ("http://host/path") are legal but our page never
    // sends one; reduce to origin-form so routing stays simple.
    if (target.startsWith(QByteArrayLiteral("http://"))
        || target.startsWith(QByteArrayLiteral("https://"))) {
        const QUrl absolute(QString::fromLatin1(target));
        target = absolute.path().toLatin1();
        const QByteArray query = absolute.query().toLatin1();
        if (!query.isEmpty())
            target += '?' + query;
    }
    const int question = target.indexOf('?');
    if (question >= 0) {
        request.path = target.left(question);
        request.query = target.mid(question + 1);
    } else {
        request.path = target;
    }

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i);
        if (line.trimmed().isEmpty())
            continue;
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        request.headers.insert(name, value);
    }

    request.body = raw.mid(headerEnd + 4);
    if (out)
        *out = request;
    return true;
}

bool KeyEntryServer::constantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

bool KeyEntryServer::hostIsLoopback(const QByteArray& host, quint16 port)
{
    if (host.isEmpty())
        return false;

    QByteArray name = host;
    quint16 hostPort = 0;

    if (name.startsWith('[')) {  // bracketed IPv6, e.g. [::1]:1234
        const int close = name.indexOf(']');
        if (close < 0)
            return false;
        const QByteArray literal = name.mid(1, close - 1);
        name = name.mid(close + 1);
        if (name.startsWith(':'))
            hostPort = name.mid(1).toUShort();
        return literal == QByteArrayLiteral("::1")
               && (hostPort == 0 || hostPort == port);
    }

    const int colon = name.lastIndexOf(':');
    if (colon >= 0) {
        hostPort = name.mid(colon + 1).toUShort();
        name = name.left(colon);
    }

    const bool loopback = name == QByteArrayLiteral("127.0.0.1")
                          || name == QByteArrayLiteral("localhost");
    return loopback && (hostPort == 0 || hostPort == port);
}

}  // namespace app
