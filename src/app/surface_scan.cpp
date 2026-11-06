#include "app/surface_scan.h"

#include "app/providers.h"
#include "app/secret_store.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int kTimeoutMs = 30000;
constexpr int kThumbnailTimeoutMs = 15000;
// SauceNAO returns its whole index ranking even for images it does not know;
// below ~0.50 the rows are unrelated noise rather than a plausible match.
constexpr double kSauceNaoMinSimilarity = 0.50;
// trace.moe answers with low-confidence frames even for images that are not
// anime. Real scene matches score ~0.95+; 0.90 keeps those and drops the noise.
constexpr double kTraceMoeMinSimilarity = 0.90;
// Total time one provider may spend fetching thumbnails before the scan moves
// on and leaves the remaining hits with their image URLs only.
constexpr int kThumbnailBudgetMs = 20000;
// Keep one noisy provider (SerpApi's Lens returns 60+ hits) from flooding the
// grid and turning the scan into a thumbnail-download marathon.
constexpr int kMaxResultsPerProvider = 24;
constexpr int kThumbnailPx = 128;
// SerpApi's Image API rejects anything over 500 KB, so target a little below.
constexpr int kSerpApiUploadLimit = 480 * 1024;

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

// A downscaled JPEG for SerpApi's Image API, which only accepts JPG/PNG/WebP
// and caps uploads at 500 KB. Quality is stepped down, then the image itself,
// until the encoded bytes fit.
QByteArray uploadJpeg(const QString& path, int maxDim = 1024)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize native = reader.size();
    if (!native.isValid() || native.isEmpty())
        return QByteArray();
    reader.setScaledSize(native.scaled(QSize(maxDim, maxDim),
                                       Qt::KeepAspectRatio));
    QImage img = reader.read();
    if (img.isNull())
        return QByteArray();

    // JPEG has no alpha channel; flatten transparency onto white first.
    if (img.hasAlphaChannel()) {
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter painter(&flat);
        painter.drawImage(0, 0, img);
        painter.end();
        img = flat;
    }

    for (int pass = 0; pass < 6; ++pass) {
        for (int quality = 90; quality >= 50; quality -= 10) {
            QByteArray out;
            QBuffer buffer(&out);
            buffer.open(QIODevice::WriteOnly);
            if (!img.save(&buffer, "JPEG", quality))
                return QByteArray();
            if (!out.isEmpty() && out.size() <= kSerpApiUploadLimit)
                return out;
        }
        img = img.scaled(img.size() * 4 / 5, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
        if (img.isNull())
            break;
    }
    return QByteArray();
}

// Turns raw downloaded image bytes into a small PNG. Running this on the worker
// thread means the GUI thread only ever decodes a 128-px image, never a
// full-size remote one.
QByteArray thumbnailFromBytes(const QByteArray& raw, int maxSize = kThumbnailPx)
{
    if (raw.isEmpty())
        return QByteArray();

    QBuffer input;
    input.setData(raw);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setAutoTransform(true);
    const QSize native = reader.size();
    if (native.isValid() && !native.isEmpty())
        reader.setScaledSize(
            native.scaled(QSize(maxSize, maxSize), Qt::KeepAspectRatio));
    QImage img = reader.read();
    if (img.isNull())
        return QByteArray();
    if (img.width() > maxSize || img.height() > maxSize)
        img = img.scaled(maxSize, maxSize, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);

    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return out;
}

app::SurfaceResult makeError(const QString& id, const QString& name,
                             const QString& message)
{
    app::SurfaceResult error;
    error.provider = id;
    error.providerName = name;
    error.error = message;
    return error;
}

// Pulls a human-readable message out of the common provider error shapes:
// {"error":"..."}, {"error":{"message":"..."}} or {"message":"..."}.
QString jsonErrorText(const QByteArray& payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return QString();
    const QJsonObject root = document.object();
    const QJsonValue error = root.value(QStringLiteral("error"));
    if (error.isString())
        return error.toString();
    if (error.isObject()) {
        const QString message =
            error.toObject().value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            return message;
    }
    return root.value(QStringLiteral("message")).toString();
}

struct HttpOutcome {
    QByteArray body;
    int status = 0;
    bool timedOut = false;
    QString error;
};

// One synchronous request with a hard wall-clock deadline. `setTransferTimeout`
// only bounds the transfer, not a connection that opens and then stalls, so a
// QTimer guarantees the scan always moves on.
HttpOutcome performRequest(QNetworkAccessManager& net, QNetworkRequest request,
                           const QByteArray& body, bool forcePost = false)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, agentString());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    deadline.start(kTimeoutMs);

    QNetworkReply* reply = (body.isEmpty() && !forcePost)
                               ? net.get(request)
                               : net.post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [&loop] { loop.quit(); });
    loop.exec();
    deadline.stop();

    HttpOutcome outcome;
    outcome.timedOut = timedOut;
    if (timedOut && reply->isRunning())
        reply->abort();
    outcome.body = reply->readAll();
    outcome.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && outcome.status == 0)
        outcome.error = reply->errorString();
    reply->deleteLater();
    return outcome;
}

// Downloads a thumbnail. Returns empty bytes on an invalid URL, a network
// error, or the deadline; a missing thumbnail must never fail the scan.
QByteArray fetchBytes(QNetworkAccessManager& net, const QString& url)
{
    const QUrl target(url);
    if (url.isEmpty() || !target.isValid())
        return QByteArray();

    QNetworkRequest request(target);
    request.setHeader(QNetworkRequest::UserAgentHeader, agentString());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kThumbnailTimeoutMs);

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    deadline.start(kThumbnailTimeoutMs);

    QNetworkReply* reply = net.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [&loop] { loop.quit(); });
    loop.exec();
    deadline.stop();

    if (timedOut && reply->isRunning())
        reply->abort();
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QByteArray bytes = ok ? reply->readAll() : QByteArray();
    reply->deleteLater();
    return bytes;
}

// --- Response parsers -------------------------------------------------------
// Each parser appends either hits or a single per-provider error row.

void parseGoogleVision(const QByteArray& payload, const QString& id,
                       const QString& name,
                       std::vector<app::SurfaceResult>& out)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QJsonArray responses =
        root.value(QStringLiteral("responses")).toArray();
    if (responses.isEmpty()) {
        out.push_back(makeError(id, name, QStringLiteral("empty response")));
        return;
    }
    const QJsonObject response = responses.first().toObject();
    if (response.contains(QStringLiteral("error"))) {
        const QString message = response.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString();
        out.push_back(makeError(
            id, name,
            message.isEmpty() ? QStringLiteral("request failed") : message));
        return;
    }

    const QJsonObject web =
        response.value(QStringLiteral("webDetection")).toObject();
    if (web.isEmpty()) {
        out.push_back(makeError(id, name, QStringLiteral("no match")));
        return;
    }

    auto add = [&](app::SurfaceResult result) {
        result.provider = id;
        result.providerName = name;
        out.push_back(result);
    };

    // Pages where the image appears are the point of a surface scan, so show
    // them before the raw image hits.
    for (const QJsonValue& value :
         web.value(QStringLiteral("pagesWithMatchingImages")).toArray()) {
        const QJsonObject page = value.toObject();
        app::SurfaceResult result;
        result.title = page.value(QStringLiteral("pageTitle")).toString();
        if (result.title.isEmpty())
            result.title = QStringLiteral("page with match");
        result.url = page.value(QStringLiteral("url")).toString();
        const QJsonArray full =
            page.value(QStringLiteral("fullMatchingImages")).toArray();
        const QJsonArray partial =
            page.value(QStringLiteral("partialMatchingImages")).toArray();
        if (!full.isEmpty())
            result.thumbnail = full.first()
                                   .toObject()
                                   .value(QStringLiteral("url"))
                                   .toString();
        else if (!partial.isEmpty())
            result.thumbnail = partial.first()
                                   .toObject()
                                   .value(QStringLiteral("url"))
                                   .toString();
        result.similarity = -1.0;
        add(result);
    }

    auto addImages = [&](const QJsonArray& images, const QString& kind) {
        for (const QJsonValue& value : images) {
            const QJsonObject image = value.toObject();
            app::SurfaceResult result;
            result.title = kind;
            result.url = image.value(QStringLiteral("url")).toString();
            // Image hits return the image itself; use it as the thumbnail too.
            result.thumbnail = result.url;
            result.similarity = -1.0;
            add(result);
        }
    };
    addImages(web.value(QStringLiteral("fullMatchingImages")).toArray(),
              QStringLiteral("full match"));
    addImages(web.value(QStringLiteral("partialMatchingImages")).toArray(),
              QStringLiteral("partial match"));
    addImages(web.value(QStringLiteral("visuallySimilarImages")).toArray(),
              QStringLiteral("visually similar"));

    // Entities and best-guess labels are text, not URLs. Point them at a Google
    // search so the row is still useful, and keep Vision's score when present.
    for (const QJsonValue& value :
         web.value(QStringLiteral("webEntities")).toArray()) {
        const QJsonObject entity = value.toObject();
        app::SurfaceResult result;
        result.title = entity.value(QStringLiteral("description")).toString();
        if (result.title.isEmpty())
            result.title = entity.value(QStringLiteral("entityType")).toString();
        if (result.title.isEmpty())
            continue;
        result.url = entity.value(QStringLiteral("entityUrl")).toString();
        if (result.url.isEmpty())
            result.url = QStringLiteral("https://www.google.com/search?q=") +
                         QString::fromLatin1(
                             QUrl::toPercentEncoding(result.title));
        result.similarity =
            entity.contains(QStringLiteral("score"))
                ? entity.value(QStringLiteral("score")).toDouble()
                : -1.0;
        result.extra = QStringLiteral("web entity");
        add(result);
    }

    for (const QJsonValue& value :
         web.value(QStringLiteral("bestGuessLabels")).toArray()) {
        const QString label =
            value.toObject().value(QStringLiteral("label")).toString();
        if (label.isEmpty())
            continue;
        app::SurfaceResult result;
        result.title = label;
        result.url = QStringLiteral("https://www.google.com/search?q=") +
                     QString::fromLatin1(QUrl::toPercentEncoding(label));
        result.similarity = -1.0;
        result.extra = QStringLiteral("best guess");
        add(result);
    }
}

void parseSauceNao(const QByteArray& payload, const QString& id,
                   const QString& name, std::vector<app::SurfaceResult>& out)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    if (root.isEmpty()) {
        out.push_back(makeError(id, name, QStringLiteral("empty response")));
        return;
    }

    const QJsonObject header = root.value(QStringLiteral("header")).toObject();
    const int status = header.value(QStringLiteral("status")).toInt(0);
    if (status < 0) {
        QString message = header.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = QStringLiteral("SauceNAO rejected the request");
        out.push_back(makeError(id, name, message));
        return;
    }
    if (status == 100) {
        out.push_back(makeError(
            id, name, QStringLiteral("rate limited — try again later")));
        return;
    }

    for (const QJsonValue& value :
         root.value(QStringLiteral("results")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QJsonObject resultHeader =
            entry.value(QStringLiteral("header")).toObject();
        const QJsonObject data = entry.value(QStringLiteral("data")).toObject();

        app::SurfaceResult result;
        result.provider = id;
        result.providerName = name;

        // similarity is a string like "34.94" in the result header.
        result.similarity =
            resultHeader.value(QStringLiteral("similarity")).toString().toDouble();
        result.similarity /= 100.0;
        if (result.similarity < kSauceNaoMinSimilarity)
            continue;

        // Title / author live under data, with a fallback to the index name if
        // the provider has no metadata.
        QString title = data.value(QStringLiteral("title")).toString();
        const QString author =
            data.value(QStringLiteral("author_name")).toString();
        if (!author.isEmpty())
            title = title.isEmpty()
                        ? author
                        : title + QStringLiteral(" — ") + author;
        result.title =
            title.isEmpty()
                ? resultHeader.value(QStringLiteral("index_name")).toString()
                : title;

        // The real source URL is the first entry of data.ext_urls (an array);
        // the author's page is a secondary link.
        const QJsonArray external =
            data.value(QStringLiteral("ext_urls")).toArray();
        result.url = external.isEmpty()
                         ? data.value(QStringLiteral("author_url")).toString()
                         : external.first().toString();
        result.thumbnail =
            resultHeader.value(QStringLiteral("thumbnail")).toString();
        out.push_back(result);
    }
}

void parseTraceMoe(const QByteArray& payload, const QString& id,
                   const QString& name, std::vector<app::SurfaceResult>& out)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QString apiError = root.value(QStringLiteral("error")).toString();
    // trace.moe returns this for an image it cannot process (e.g. not anime);
    // treat it as "no match" rather than an error the user must act on.
    if (!apiError.isEmpty() &&
        apiError != QLatin1String("Failed to process image")) {
        out.push_back(makeError(id, name, apiError));
        return;
    }

    QJsonArray results = root.value(QStringLiteral("result")).toArray();
    // A single-image request returns a flat array; a vector/batch request
    // returns one array per input. Flatten defensively.
    if (!results.isEmpty() && results.first().isArray())
        results = results.first().toArray();

    for (const QJsonValue& value : results) {
        const QJsonObject entry = value.toObject();
        app::SurfaceResult result;
        result.provider = id;
        result.providerName = name;

        // The API has no anime title, only the source filename.
        result.title = entry.value(QStringLiteral("filename")).toString();
        if (result.title.isEmpty())
            result.title = QStringLiteral("anime scene");

        const int anilist = entry.value(QStringLiteral("anilist")).toInt(0);
        if (anilist > 0)
            result.url =
                QStringLiteral("https://anilist.co/anime/%1").arg(anilist);

        result.thumbnail = entry.value(QStringLiteral("image")).toString();

        const int episode = entry.value(QStringLiteral("episode")).toInt(0);
        const double from = entry.value(QStringLiteral("from")).toDouble(0);
        const double to = entry.value(QStringLiteral("to")).toDouble(0);
        if (episode > 0)
            result.extra = QStringLiteral("episode %1 · %2s–%3s")
                               .arg(episode)
                               .arg(from, 0, 'f', 1)
                               .arg(to, 0, 'f', 1);

        // trace.moe already reports similarity in the 0–1 range. Drop the
        // low-confidence frames it returns for images that are not anime.
        result.similarity =
            entry.value(QStringLiteral("similarity")).toDouble(-1.0);
        if (result.similarity >= 0.0 &&
            result.similarity < kTraceMoeMinSimilarity)
            continue;
        out.push_back(result);
    }
}

void parseSerpApi(const QByteArray& payload, const QString& id,
                  const QString& name, std::vector<app::SurfaceResult>& out)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QString error = root.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        out.push_back(makeError(id, name, error));
        return;
    }

    auto add = [&](const QJsonArray& matches, const QString& kind) {
        for (const QJsonValue& value : matches) {
            const QJsonObject match = value.toObject();
            app::SurfaceResult result;
            result.provider = id;
            result.providerName = name;
            result.title = match.value(QStringLiteral("title")).toString();
            if (result.title.isEmpty())
                result.title = kind;
            const QString source =
                match.value(QStringLiteral("source")).toString();
            if (!source.isEmpty())
                result.extra = source;
            result.url = match.value(QStringLiteral("link")).toString();
            result.thumbnail =
                match.value(QStringLiteral("thumbnail")).toString();
            if (result.thumbnail.isEmpty())
                result.thumbnail = match.value(QStringLiteral("image")).toString();
            result.similarity = -1.0;
            out.push_back(result);
        }
    };
    add(root.value(QStringLiteral("exact_matches")).toArray(),
        QStringLiteral("exact match"));
    add(root.value(QStringLiteral("visual_matches")).toArray(),
        QStringLiteral("visual match"));
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
    for (const Provider& provider : providers()) {
        if (!reverseProviders.contains(provider.id))
            continue;
        const StoredSecret secret = store_->load(provider.id);
        if (provider.needsKey && !secret.found())
            continue;  // no key → not enabled
        targets.append(&provider);
    }

    const int total = targets.size();
    QNetworkAccessManager net;
    int done = 0;

    for (const Provider* provider : targets) {
        if (cancel && cancel())
            break;

        if (onProgress)
            onProgress(done, total, provider->name);

        const QString id = provider->id;
        const QString name = provider->name;
        const QString key = store_->load(id).value;
        const size_t firstResult = results.size();

        auto fail = [&](const QString& message) {
            results.push_back(makeError(id, name, message));
        };
        auto failHttp = [&](const HttpOutcome& outcome) {
            QString message = jsonErrorText(outcome.body);
            if (message.isEmpty())
                message = outcome.error;
            if (message.isEmpty())
                message = QStringLiteral("HTTP %1").arg(outcome.status);
            fail(message);
        };

        // Runs one provider and appends its hits or its single error row.
        auto runProvider = [&] {
            if (id == QLatin1String("zenserp")) {
                // Zenserp's reverse endpoint only accepts a publicly reachable
                // image URL; this build searches local files, so it cannot work.
                fail(QStringLiteral("needs a publicly hosted image; local "
                                    "files are not supported"));
                return;
            }

            if (id == QLatin1String("serpapi")) {
                // SerpApi's Lens engine can't fetch a local path. Upload the
                // image to SerpApi's Image API first, then search by image_id.
                const QByteArray jpeg = uploadJpeg(imagePath);
                if (jpeg.isEmpty()) {
                    fail(QStringLiteral("could not read the image"));
                    return;
                }

                const QString boundary = QStringLiteral("----LucidGraspSerpApi");
                QByteArray multipart;
                multipart +=
                    QStringLiteral("--%1\r\n").arg(boundary).toUtf8();
                multipart +=
                    "Content-Disposition: form-data; name=\"image\"; "
                    "filename=\"upload.jpg\"\r\n";
                multipart += "Content-Type: image/jpeg\r\n\r\n";
                multipart += jpeg;
                multipart +=
                    QStringLiteral("\r\n--%1--\r\n").arg(boundary).toUtf8();

                QUrl uploadUrl(QStringLiteral("https://serpapi.com/image"));
                QUrlQuery uploadQuery;
                uploadQuery.addQueryItem(QStringLiteral("api_key"), key);
                uploadUrl.setQuery(uploadQuery);

                QNetworkRequest uploadRequest(uploadUrl);
                uploadRequest.setHeader(
                    QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("multipart/form-data; boundary=") + boundary);

                const HttpOutcome upload =
                    performRequest(net, uploadRequest, multipart, true);
                if (upload.timedOut) {
                    fail(QStringLiteral("timed out uploading the image"));
                    return;
                }
                if (!upload.error.isEmpty()) {
                    fail(upload.error);
                    return;
                }

                const QString imageId =
                    QJsonDocument::fromJson(upload.body)
                        .object()
                        .value(QStringLiteral("image_id"))
                        .toString();
                if (imageId.isEmpty()) {
                    QString message = jsonErrorText(upload.body);
                    if (message.isEmpty())
                        message = QStringLiteral("image upload failed (HTTP %1)")
                                      .arg(upload.status);
                    fail(message);
                    return;
                }

                QUrl searchUrl(QStringLiteral("https://serpapi.com/search"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("engine"),
                                   QStringLiteral("google_lens"));
                query.addQueryItem(QStringLiteral("type"), QStringLiteral("all"));
                query.addQueryItem(QStringLiteral("image_id"), imageId);
                query.addQueryItem(QStringLiteral("api_key"), key);
                searchUrl.setQuery(query);

                const HttpOutcome search =
                    performRequest(net, QNetworkRequest(searchUrl), QByteArray());
                if (search.timedOut) {
                    fail(QStringLiteral("timed out"));
                    return;
                }
                if (!search.error.isEmpty()) {
                    fail(search.error);
                    return;
                }
                if (search.status >= 400) {
                    failHttp(search);
                    return;
                }
                parseSerpApi(search.body, id, name, results);
                return;
            }

            if (id == QLatin1String("google_vision")) {
                const QString base64 = downscaledImageBase64(imagePath);
                if (base64.isEmpty()) {
                    fail(QStringLiteral("could not read the image"));
                    return;
                }

                QJsonObject image{{QStringLiteral("content"), base64}};
                QJsonObject feature{
                    {QStringLiteral("type"), QStringLiteral("WEB_DETECTION")}};
                QJsonArray features;
                features.append(feature);
                QJsonObject jsonRequest;
                jsonRequest.insert(QStringLiteral("image"), image);
                jsonRequest.insert(QStringLiteral("features"), features);
                QJsonArray requests;
                requests.append(jsonRequest);
                QJsonObject root;
                root.insert(QStringLiteral("requests"), requests);

                QUrl url(QStringLiteral(
                    "https://vision.googleapis.com/v1/images:annotate"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("key"), key);
                url.setQuery(query);

                QNetworkRequest request(url);
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/json"));
                const HttpOutcome outcome = performRequest(
                    net, request,
                    QJsonDocument(root).toJson(QJsonDocument::Compact));
                if (outcome.timedOut) {
                    fail(QStringLiteral("timed out"));
                    return;
                }
                if (!outcome.error.isEmpty()) {
                    fail(outcome.error);
                    return;
                }
                if (outcome.status >= 400) {
                    failHttp(outcome);
                    return;
                }
                parseGoogleVision(outcome.body, id, name, results);
                return;
            }

            if (id == QLatin1String("saucenao")) {
                const QByteArray bytes = downscaledImageBytes(imagePath);
                if (bytes.isEmpty()) {
                    fail(QStringLiteral("could not read the image"));
                    return;
                }

                const QString boundary = QStringLiteral("----LucidGraspBoundary");
                QByteArray multipart;
                auto part = [&multipart,
                             &boundary](const char* field, const QByteArray& value) {
                    multipart += QStringLiteral("--%1\r\n").arg(boundary).toUtf8() +
                                 QByteArray("Content-Disposition: form-data; "
                                            "name=\"") +
                                 field + "\"\r\n\r\n";
                    multipart += value + "\r\n";
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
                    multipart +=
                        QStringLiteral("--%1\r\n").arg(boundary).toUtf8() +
                        QByteArray("Content-Disposition: form-data; "
                                   "name=\"file\"; filename=\"") +
                        fileName.toUtf8() + "\"\r\n" +
                        "Content-Type: application/octet-stream\r\n\r\n";
                    multipart += bytes;
                    multipart +=
                        QStringLiteral("\r\n--%1--\r\n").arg(boundary).toUtf8();
                }

                QNetworkRequest request(
                    QUrl(QStringLiteral("https://saucenao.com/search.php")));
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("multipart/form-data; boundary=") +
                                      boundary);

                const HttpOutcome outcome =
                    performRequest(net, request, multipart, true);
                if (outcome.timedOut) {
                    fail(QStringLiteral("timed out"));
                    return;
                }
                if (!outcome.error.isEmpty()) {
                    fail(outcome.error);
                    return;
                }
                if (outcome.status >= 400) {
                    failHttp(outcome);
                    return;
                }
                parseSauceNao(outcome.body, id, name, results);
                return;
            }

            if (id == QLatin1String("trace_moe")) {
                const QByteArray bytes = downscaledImageBytes(imagePath);
                if (bytes.isEmpty()) {
                    fail(QStringLiteral("could not read the image"));
                    return;
                }

                // trace.moe is POST-only (GET returns 405) and takes the image
                // as the raw request body. Sending a form field or base64 makes
                // it answer 400 "Failed to process image".
                QUrl url(QStringLiteral("https://api.trace.moe/search"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
                url.setQuery(query);

                QNetworkRequest request(url);
                if (!key.isEmpty())
                    request.setRawHeader("x-trace-key", key.toUtf8());
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("image/png"));

                const HttpOutcome outcome = performRequest(net, request, bytes);
                if (outcome.timedOut) {
                    fail(QStringLiteral("timed out"));
                    return;
                }
                if (!outcome.error.isEmpty()) {
                    fail(outcome.error);
                    return;
                }
                if (outcome.status >= 400) {
                    // A valid image that simply isn't an anime frame is a
                    // no-match, not a failure the user needs to see.
                    if (outcome.status == 400 &&
                        jsonErrorText(outcome.body) ==
                            QLatin1String("Failed to process image"))
                        return;
                    failHttp(outcome);
                    return;
                }
                parseTraceMoe(outcome.body, id, name, results);
                return;
            }
        };

        runProvider();
        ++done;

        // Bound one provider's output so the grid stays readable.
        if (results.size() - firstResult >
            static_cast<size_t>(kMaxResultsPerProvider))
            results.resize(firstResult + kMaxResultsPerProvider);

        // Download the thumbnails here, on the worker thread, and hand the GUI
        // thread small pre-scaled PNGs. The old code fetched them on the GUI
        // thread, where a slow host could freeze the window for minutes.
        bool needsThumbnails = false;
        for (size_t i = firstResult; i < results.size(); ++i) {
            if (results[i].error.isEmpty() &&
                !results[i].thumbnail.isEmpty() &&
                results[i].thumbnailData.isEmpty()) {
                needsThumbnails = true;
            }
        }
        if (needsThumbnails && onProgress)
            onProgress(done, total, name + QStringLiteral(" · images"));

        // Even with a per-image deadline, fetching 24 thumbnails one after the
        // other could stall the scan for minutes on a slow host. Cap the whole
        // thumbnail phase per provider; hits keep their URLs either way.
        QElapsedTimer thumbnailClock;
        thumbnailClock.start();
        for (size_t i = firstResult; i < results.size(); ++i) {
            if (cancel && cancel())
                break;
            if (thumbnailClock.elapsed() >= kThumbnailBudgetMs)
                break;
            SurfaceResult& result = results[i];
            if (!result.error.isEmpty() || result.thumbnail.isEmpty() ||
                !result.thumbnailData.isEmpty())
                continue;
            const QByteArray raw = fetchBytes(net, result.thumbnail);
            result.thumbnailData = thumbnailFromBytes(raw);
        }
    }

    return results;
}

}  // namespace app
