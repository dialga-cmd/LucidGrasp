#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace app {

// A surface-scan provider as the key-entry page sees it. This is pure data:
// no network, no storage, so the registry can be unit-tested and the page can
// be rendered from it without pulling in the HTTP layer.
struct Provider {
    QString id;            // stable: drives the env var, INI key and keyring entry
    QString name;          // full display name
    QString shortName;     // tight label for the square tile
    QString category;      // groups rows on the page
    QString description;   // one line, shown in the detail panel
    QString signupUrl;     // where to obtain a key (empty when none is needed)
    bool needsKey = true;  // false for anonymous free tiers (trace.moe)
    QString validator;     // selector read by KeyEntryServer; unused by this file
    // Beginner walkthrough shown by the "How to get a key" button. Each entry
    // is one action, in order, assuming the reader has never used the site.
    QStringList steps;
};

// Every supported provider, in the order the page should show them. Only
// platforms that are self-serve and grant recurring credits are listed (see
// "image - surface scan plan.md" §9).
const QVector<Provider>& providers();

// Null when the id is unknown. Callers must handle null rather than assume.
const Provider* providerById(const QString& id);

}  // namespace app
