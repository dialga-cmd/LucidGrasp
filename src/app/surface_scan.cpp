#include "app/surface_scan.h"

#include "app/providers.h"
#include "app/secret_store.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

int kTimeoutMs = 30000;

QString agentString()
{
    return QStringLiteral("LucidGrasp/") + QCoreApplication::applicationVersion();
}

// Reads the image, downscales it to a sensible maximum dimension, and returns
// the re-encoded PNG as raw bytes. Reverse-image APIs work on the reduced
// image, and this keeps both the request body and any local decode within
// bounds. Without this, an 8 MB camera photo becomes ~11 MB of base64 (and a
// huge QImage decode), which is what trips Qt's allocation limit and the
// providers' input caps.
//
// Returns an empty QByteArray when the file can't be read as an image.
QByteArray downscaledImageBytes(const QString& path, int maxDim = 1024)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Let the reader report the real size without loading pixels, then cap the
    // decode to the target so a 4000-px photo is never decoded at full res.
    const QSize native = reader.size();
    if (!native.isValid() || native.isEmpty())
        return QByteArray();
    reader.setScaledSize(native.scaled(QSize(maxDim, maxDim),
                                       Qt::KeepAspectRatio));
    QImage img = reader.read();
    if (img.isNull())
        return QByteArray();

    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return out;
}

// Same as above but base64-encoded, for providers that take an in-line image.
QString downscaledImageBase64(const QString& path, int maxDim = 1024)
{
    const QByteArray bytes = downscaledImageBytes(path, maxDim);
    return bytes.isEmpty() ? QString() : QString::fromLatin1(bytes.toBase64());
}

}  // namespace

namespace app {

SurfaceScan::SurfaceScan(SecretStore* store) : store_(store) {}

std::vector<SurfaceResult> SurfaceScan::search(
    const QString& imagePath,
    const SurfaceProgress& onProgress,
    const std::function<bool()>& cancel)
{
    std::vector<SurfaceResult> results;

    // The reverse-native providers are the ones that actually turn an image
    // into web results. The keyword-only rows in the registry are second-stage
    // and are not called here (see the plan, §5).
    const QStringList reverseProviders = {
        QStringLiteral("google_vision"),
        QStringLiteral("saucenao"),
        QStringLiteral("trace_moe"),
        QStringLiteral("serpapi"),
        QStringLiteral("zenserp"),
    };

    // Collect the ones that have a key (trace_moe needs none).
    QVector<const Provider*> targets;
    for (const Provider& p : providers()) {
        if (!reverseProviders.contains(p.id))
            continue;
        const StoredSecret secret = store_->load(p.id);
        if (p.needsKey && !secret.found())
            continue;  // no key → not enabled
        targets.append(&p);
    }

    const int total = targets.size();
    QNetworkAccessManager net;

    int done = 0;
    for (const Provider* provider : targets) {
        if (cancel && cancel())
            break;

        if (onProgress)
            onProgress(done, total, provider->id);

        const StoredSecret secret = store_->load(provider->id);
        const QString key = secret.value;

        // --- Build the request for this provider ----------------------------
        QNetworkRequest request;
        bool isMultipart = false;
        QByteArray body;
        QString queryExtra;

        const QString& id = provider->id;

        if (id == QLatin1String("google_vision")) {
            const QString b64 = downscaledImageBase64(imagePath);
            if (b64.isEmpty()) {
                SurfaceResult e;
                e.provider = id;
                e.providerName = provider->name;
                e.error = QStringLiteral("could not read the image");
                results.push_back(e);
                ++done;
                continue;
            }
            QJsonObject image{{QStringLiteral("content"), b64}};
            QJsonObject feature{{QStringLiteral("type"),
                                 QStringLiteral("WEB_DETECTION")}};
            QJsonArray features{feature};
            QJsonObject one{{QStringLiteral("image"), image},
                            {QStringLiteral("features"), features}};
            QJsonArray requests{one};
            QJsonObject root{{QStringLiteral("requests"), requests}};

            QUrl url(QStringLiteral(
                "https://vision.googleapis.com/v1/images:annotate"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("key"), key);
            url.setQuery(q);

            request.setUrl(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/json"));
            body = QJsonDocument(root).toJson(QJsonDocument::Compact);
        } else if (id == QLatin1String("saucenao")) {
            // multipart form upload of a downscaled copy of the image
            const QByteArray bytes = downscaledImageBytes(imagePath);
            if (bytes.isEmpty()) {
                SurfaceResult e;
                e.provider = id;
                e.providerName = provider->name;
                e.error = QStringLiteral("could not read the image");
                results.push_back(e);
                ++done;
                continue;
            }
            const QString boundary = QStringLiteral("----LucidGraspBoundary");
            QByteArray mp;
            auto part = [&mp, &boundary](const char* name, const QByteArray& value) {
                mp += QString("--%1\r\n").arg(boundary).toUtf8()
                    + QByteArray("Content-Disposition: form-data; name=\"")
                    + name + "\"\r\n\r\n";
                mp += value + "\r\n";
            };
            part("api_key", key.toUtf8());
            part("output_type", "2");
            part("db", "999");
            part("numres", "10");
            {
                const QString fileName =
                    QFileInfo(imagePath).fileName().isEmpty()
                        ? QStringLiteral("upload.png")
                        : QFileInfo(imagePath).fileName();
                mp += QString("--%1\r\n").arg(boundary).toUtf8()
                    + QByteArray("Content-Disposition: form-data; "
                                 "name=\"file\"; filename=\"")
                    + fileName.toUtf8() + "\"\r\n"
                    + "Content-Type: application/octet-stream\r\n\r\n";
                mp += bytes;
                mp += QString("\r\n--%1--\r\n").arg(boundary).toUtf8();
            }
            body = mp;
            request.setUrl(QUrl(QStringLiteral("https://saucenao.com/search.php")));
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("multipart/form-data; boundary=") +
                                  boundary);
            isMultipart = true;
        } else if (id == QLatin1String("trace_moe")) {
            const QString b64 = downscaledImageBase64(imagePath);
            if (b64.isEmpty()) {
                SurfaceResult e;
                e.provider = id;
                e.providerName = provider->name;
                e.error = QStringLiteral("could not read the image");
                results.push_back(e);
                ++done;
                continue;
            }
            // trace.moe is POST-only (GET returns 405). The image goes in a
            // form field, not a query parameter. Base64 contains + / = which
            // are form-reserved, so the value must be URL-encoded.
            QUrl url(QStringLiteral("https://api.trace.moe/search"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
            url.setQuery(q);
            request.setUrl(url);
            if (!key.isEmpty())
                request.setRawHeader("x-trace-key", key.toUtf8());
            // The base64 string contains +, / and = which are reserved in a
            // form body. QUrl::toPercentEncoding handles all three.
            body = QStringLiteral("image=%1")
                       .arg(QUrl::toPercentEncoding(b64))
                       .toUtf8();
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/x-www-form-urlencoded"));
        } else if (id == QLatin1String("serpapi")) {
            QUrl url(QStringLiteral("https://serpapi.com/search"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("engine"), QStringLiteral("google_reverse_image"));
            q.addQueryItem(QStringLiteral("api_key"), key);
            // SerpApi accepts an uploaded file via the "file" multipart field
            // or a URL; we pass the local path as a query param it can fetch.
            q.addQueryItem(QStringLiteral("url"), imagePath);
            url.setQuery(q);
            request.setUrl(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/x-www-form-urlencoded"));
            body = q.query(QUrl::FullyEncoded).toUtf8();
        } else if (id == QLatin1String("zenserp")) {
            QUrl url(QStringLiteral("https://app.zenserp.com/api/v2/search"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("apikey"), key);
            q.addQueryItem(QStringLiteral("search_type"), QStringLiteral("image"));
            q.addQueryItem(QStringLiteral("url"), imagePath);
            url.setQuery(q);
            request.setUrl(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/x-www-form-urlencoded"));
            body = q.query(QUrl::FullyEncoded).toUtf8();
        } else {
            ++done;
            continue;
        }

        request.setHeader(QNetworkRequest::UserAgentHeader, agentString());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(kTimeoutMs);

        // --- Run the request synchron on the calling thread's event loop ----
        // The caller (a worker std::thread, see mainwindow.cpp) has no event
        // loop, so we run a local one just for the duration of this request.
        //
        // setTransferTimeout only bounds the *transfer*, not a connection that
        // stalls after the headers: a provider that opens the socket and then
        // sends nothing would otherwise hang loop.exec() for minutes. A hard
        // wall-clock deadline guarantees the scan moves on no matter what.
        QEventLoop loop;
        auto* deadline = new QTimer(&loop);
        deadline->setSingleShot(true);
        bool timedOut = false;
        QObject::connect(deadline, &QTimer::timeout, &loop,
                         [&loop, &timedOut] {
                           timedOut = true;
                           loop.quit();
                         });
        deadline->start(kTimeoutMs);

        QNetworkReply* reply =
            (body.isEmpty() && !isMultipart)
                ? net.get(request)
                : net.post(request, body);
        QObject::connect(reply, &QNetworkReply::finished, &loop,
                         [&loop] { loop.quit(); });
        loop.exec();
        deadline->stop();

        // When the deadline fired first the reply is still in flight; abort it
        // so it can't outlive this iteration and leak a socket.
        if (timedOut && reply->isRunning())
            reply->abort();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (timedOut) {
            SurfaceResult e;
            e.provider = id;
            e.providerName = provider->name;
            e.error = QStringLiteral("timed out");
            results.push_back(e);
            ++done;
            continue;
        }

        // --- Parse per provider ---------------------------------------------
        if (id == QLatin1String("google_vision")) {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            const QJsonArray reqs = root.value(QStringLiteral("responses")).toArray();
            if (!reqs.isEmpty()) {
                const QJsonObject resp = reqs.first().toObject();
                const QJsonObject web =
                    resp.value(QStringLiteral("webDetection")).toObject();
                const QJsonObject entities =
                    web.value(QStringLiteral("webEntities")).toObject();
                const QJsonArray pages =
                    web.value(QStringLiteral("pagesWithMatchingImages")).toArray();
                const QJsonArray full =
                    web.value(QStringLiteral("fullMatchingImages")).toArray();
                const QJsonArray partial =
                    web.value(QStringLiteral("partialMatchingImages")).toArray();
                const QJsonArray similar =
                    web.value(QStringLiteral("visuallySimilarImages")).toArray();

                // webEntities → keyword-style results
                for (const auto& e : entities) {
                    const QJsonObject eo = e.toObject();
                    SurfaceResult r;
                    r.provider = id;
                    r.providerName = provider->name;
                    r.title = eo.value(QStringLiteral("description")).toString()
                                  .isEmpty()
                                   ? eo.value(QStringLiteral("entityType"))
                                         .toString()
                                   : QString();
                    r.url = eo.value(QStringLiteral("entityUrl")).toString();
                    r.similarity = -1.0;
                    results.push_back(r);
                }
                auto addImages = [&](const QJsonArray& arr, const char* kind) {
                    for (const auto& v : arr) {
                        const QJsonObject vo = v.toObject();
                        SurfaceResult r;
                        r.provider = id;
                        r.providerName = provider->name;
                        r.title = QString::fromLatin1(kind);
                        r.url = vo.value(QStringLiteral("url")).toString();
                        r.thumbnail =
                            vo.value(QStringLiteral("thumbnailUrl")).toString();
                        r.similarity = -1.0;
                        results.push_back(r);
                    }
                };
                addImages(full, "full match");
                addImages(partial, "partial match");
                addImages(pages, "page with match");
                addImages(similar, "visually similar");
            }
        } else if (id == QLatin1String("saucenao")) {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            const QJsonObject header =
                root.value(QStringLiteral("header")).toObject();
            const int hStatus = header.value(QStringLiteral("status")).toInt(0);
            if (hStatus < 0) {
                SurfaceResult e;
                e.provider = id;
                e.providerName = provider->name;
                e.error = header.value(QStringLiteral("message")).toString()
                              .isEmpty()
                           ? QStringLiteral("SauceNAO returned no results")
                           : header.value(QStringLiteral("message")).toString();
                results.push_back(e);
            } else {
                const QJsonArray resultsArr =
                    root.value(QStringLiteral("results")).toArray();
                for (const auto& r : resultsArr) {
                    const QJsonObject ro = r.toObject();
                    const QJsonObject header =
                        ro.value(QStringLiteral("header")).toObject();
                    const QJsonObject data =
                        ro.value(QStringLiteral("data")).toObject();

                    SurfaceResult s;
                    s.provider = id;
                    s.providerName = provider->name;

                    // similarity is a string like "34.94" in the result header.
                    s.similarity =
                        header.value(QStringLiteral("similarity"))
                            .toString()
                            .toDouble();
                    s.similarity = s.similarity / 100.0;

                    // Title / author live under data, with a fallback to the
                    // index name if the provider has no metadata.
                    QString title = data.value(QStringLiteral("title")).toString();
                    const QString author =
                        data.value(QStringLiteral("author_name")).toString();
                    if (!author.isEmpty())
                        title = title.isEmpty()
                            ? author
                            : title + QStringLiteral(" — ") + author;
                    s.title = title.isEmpty()
                                  ? header.value(QStringLiteral("index_name")).toString()
                                  : title;

                    // The real source URL is the first entry of data.ext_urls
                    // (an array); the author's page is a secondary link.
                    const QJsonArray ext =
                        data.value(QStringLiteral("ext_urls")).toArray();
                    s.url = ext.isEmpty()
                               ? data.value(QStringLiteral("author_url")).toString()
                               : ext.first().toString();
                    s.thumbnail =
                        header.value(QStringLiteral("thumbnail")).toString();
                    results.push_back(s);
                }
            }
        } else if (id == QLatin1String("trace_moe")) {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            const QJsonArray arr = root.value(QStringLiteral("result")).toArray();
            for (const auto& r : arr) {
                const QJsonObject ro = r.toObject();
                SurfaceResult s;
                s.provider = id;
                s.providerName = provider->name;
                const QJsonObject anilist =
                    ro.value(QStringLiteral("anilist")).toObject();
                const QJsonObject mal = ro.value(QStringLiteral("mal")).toObject();
                s.title = anilist.value(QStringLiteral("title"))
                              .toObject()
                              .value(QStringLiteral("display"))
                              .toString()
                              .isEmpty()
                           ? mal.value(QStringLiteral("title")).toString()
                           : anilist.value(QStringLiteral("title"))
                                 .toObject()
                                 .value(QStringLiteral("display"))
                                 .toString();
                const double from =
                    ro.value(QStringLiteral("from")).toDouble(0);
                const double to = ro.value(QStringLiteral("to")).toDouble(0);
                const int ep =
                    ro.value(QStringLiteral("episode")).toInt(0);
                s.extra = QStringLiteral("episode %1, %2s–%3s")
                             .arg(ep)
                             .arg(from, 0, 'f', 1)
                             .arg(to, 0, 'f', 1);
                s.url = QStringLiteral("https://myanimelist.net/anime/")
                          + mal.value(QStringLiteral("id")).toString();
                s.thumbnail = ro.value(QStringLiteral("preview"))
                                  .toObject()
                                  .value(QStringLiteral("url"))
                                  .toString();
                const double sim =
                    ro.value(QStringLiteral("similarity")).toDouble(0);
                // trace.moe reports similarity as a 0-100 percentage
                s.similarity = sim / 100.0;
                results.push_back(s);
            }
        } else if (id == QLatin1String("serpapi")) {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            const QJsonArray matches =
                root.value(QStringLiteral("visual_matches")).toArray();
            const QJsonArray exact =
                root.value(QStringLiteral("exact_matches")).toArray();
            auto addLens = [&](const QJsonArray& arr, const char* kind) {
                for (const auto& m : arr) {
                    const QJsonObject mo = m.toObject();
                    SurfaceResult s;
                    s.provider = id;
                    s.providerName = provider->name;
                    s.title = QString::fromLatin1(kind);
                    s.url = mo.value(QStringLiteral("link")).toString();
                    s.thumbnail = mo.value(QStringLiteral("thumbnail")).toString();
                    s.similarity = -1.0;
                    results.push_back(s);
                }
            };
            addLens(exact, "exact match");
            addLens(matches, "visual match");
        } else if (id == QLatin1String("zenserp")) {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            // Zenserp's image result shape; be defensive about the container.
            const QJsonArray matches =
                root.value(QStringLiteral("images")).toArray();
            for (const auto& m : matches) {
                const QJsonObject mo = m.toObject();
                SurfaceResult s;
                s.provider = id;
                s.providerName = provider->name;
                s.title = mo.value(QStringLiteral("title")).toString();
                s.url = mo.value(QStringLiteral("url")).toString();
                s.thumbnail = mo.value(QStringLiteral("thumbnail")).toString();
                s.similarity = -1.0;
                results.push_back(s);
            }
        }

        ++done;
    }

    return results;
}

QPixmap SurfaceScan::thumbnailPixmap(const QString& url, int maxSize) {
    if (url.isEmpty())
      return QPixmap();

    QNetworkRequest req;
    req.setUrl(QUrl(url));
    req.setHeader(QNetworkRequest::UserAgentHeader, agentString());
    req.setTransferTimeout(15000);

    QNetworkAccessManager net;
    QEventLoop loop;
    bool timedOut = false;
    auto* deadline = new QTimer(&loop);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, &loop,
                     [&loop, &timedOut] {
                       timedOut = true;
                       loop.quit();
                     });
    deadline->start(15000);

    QNetworkReply* reply = net.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [&loop] { loop.quit(); });
    loop.exec();
    deadline->stop();

    if (timedOut && reply->isRunning())
      reply->abort();
    const QByteArray bytes = reply->readAll();
    reply->deleteLater();

    if (bytes.isEmpty())
      return QPixmap();

    QImage img;
    img.loadFromData(bytes);
    if (img.isNull())
      return QPixmap();
    // Scale to the grid size so a 2000-px remote image isn't rendered huge.
    // Do the scale on the QImage first, then build the pixmap from the result,
    // so a format the pixmap path can't handle still yields a viewable image.
    QImage scaled = img.scaled(QSize(maxSize, maxSize),
                               Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    if (scaled.isNull())
      scaled = img;
    return QPixmap::fromImage(scaled);
}

}  // namespace app
