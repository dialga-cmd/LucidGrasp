#include "app/providers.h"

namespace app {

const QVector<Provider>& providers()
{
    // Clang-format off
    static const QVector<Provider> list = {
        // --- Official reverse-image-search platforms -------------------------
        {
            QStringLiteral("google_vision"),
            QStringLiteral("Google Cloud Vision — Web Detection"),
            QStringLiteral("Google Vision"),
            QStringLiteral("Official reverse image search"),
            QStringLiteral("Google's official web-wide reverse lookup: full/partial matches, pages, visually similar images."),
            QStringLiteral("https://console.cloud.google.com/apis/credentials"),
            true,
            QStringLiteral("google_vision"),
            {
                QStringLiteral("Open console.cloud.google.com and sign in with a Google account."),
                QStringLiteral("If asked, accept the terms. Create a project: click the project picker in the top bar → New project → name it → Create."),
                QStringLiteral("In the search box at the top, type \"Cloud Vision API\", open it, and click Enable."),
                QStringLiteral("Enable billing for the project. Vision includes 1,000 free units every month, but Google still requires a billing account: Billing → Link a billing account → add a card."),
                QStringLiteral("Go to APIs & Services → Credentials → Create credentials → API key."),
                QStringLiteral("Copy the key that appears. (Optional: click Restrict key and limit it to the Cloud Vision API.)"),
                QStringLiteral("Paste the key here and click Validate."),
            },
        },
        {
            QStringLiteral("saucenao"),
            QStringLiteral("SauceNAO"),
            QStringLiteral("SauceNAO"),
            QStringLiteral("Official reverse image search"),
            QStringLiteral("Reverse-search artwork and anime against known source databases. Free registered API."),
            QStringLiteral("https://saucenao.com/user.php?page=search-api"),
            true,
            QStringLiteral("saucenao"),
            {
                QStringLiteral("Open saucenao.com and click Register (or log in if you already have an account)."),
                QStringLiteral("Create an account with a username, email and password, then confirm your email."),
                QStringLiteral("While logged in, open the API page: saucenao.com/user.php?page=search-api."),
                QStringLiteral("Copy the API key shown on that page."),
                QStringLiteral("Paste it here and click Validate. Free keys are rate-limited, so results are cached."),
            },
        },
        {
            QStringLiteral("trace_moe"),
            QStringLiteral("trace.moe"),
            QStringLiteral("trace.moe"),
            QStringLiteral("Official reverse image search"),
            QStringLiteral("Identify the anime scene a screenshot came from. Anonymous use needs no key."),
            QStringLiteral("https://trace.moe/account"),
            false,
            QStringLiteral("trace_moe"),
            {
                QStringLiteral("No key is needed — trace.moe works anonymously."),
                QStringLiteral("Optional: to raise the rate limit, create an account at trace.moe/account and copy the donor API key."),
                QStringLiteral("Click Test to confirm trace.moe is reachable."),
            },
        },

        // --- Third-party reverse-image proxies -------------------------------
        {
            QStringLiteral("serpapi"),
            QStringLiteral("SerpApi"),
            QStringLiteral("SerpApi"),
            QStringLiteral("Third-party reverse image search"),
            QStringLiteral("Wraps Google Lens, Google Reverse Image and Yandex reverse image behind one JSON API."),
            QStringLiteral("https://serpapi.com/users/sign_up"),
            true,
            QStringLiteral("serpapi"),
            {
                QStringLiteral("Go to serpapi.com and click Sign up."),
                QStringLiteral("Enter your email and a password, then confirm your email address."),
                QStringLiteral("Log in and open your Dashboard (serpapi.com/dashboard)."),
                QStringLiteral("Copy the \"Your API Key\" value shown on the dashboard."),
                QStringLiteral("The free plan includes 250 searches/month. A card may be requested to prevent abuse, but the free plan is not charged."),
                QStringLiteral("Paste the key here and click Validate."),
            },
        },
        {
            QStringLiteral("zenserp"),
            QStringLiteral("Zenserp"),
            QStringLiteral("Zenserp"),
            QStringLiteral("Third-party reverse image search"),
            QStringLiteral("Explicit Google Reverse Image Search endpoint plus regular Google/Bing/Yandex SERP."),
            QStringLiteral("https://zenserp.com/"),
            true,
            QStringLiteral("zenserp"),
            {
                QStringLiteral("Go to zenserp.com and click Get API Key / Sign up."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("Open the Dashboard — your API key is shown there."),
                QStringLiteral("Copy it. The free plan includes 50 searches/month."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },

        // --- Keyword / general (used after the image -> keyword bridge) ------
        {
            QStringLiteral("hasdata"),
            QStringLiteral("HasData"),
            QStringLiteral("HasData"),
            QStringLiteral("Keyword / general"),
            QStringLiteral("Google Images results as JSON from a text query. Second stage after the image-to-keyword bridge."),
            QStringLiteral("https://hasdata.com/"),
            true,
            QStringLiteral("hasdata"),
            {
                QStringLiteral("Go to hasdata.com and click Sign up."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("Open the Dashboard and go to API Keys."),
                QStringLiteral("Copy your API key. The free plan includes 1,000 credits/month."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },
        {
            QStringLiteral("scraperapi"),
            QStringLiteral("ScraperAPI"),
            QStringLiteral("ScraperAPI"),
            QStringLiteral("Keyword / general"),
            QStringLiteral("General scraping with proxy rotation. Text search only; no reverse route."),
            QStringLiteral("https://www.scraperapi.com/signup/"),
            true,
            QStringLiteral("scraperapi"),
            {
                QStringLiteral("Go to scraperapi.com and click Start free / Sign up."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("Your API key is shown at the top of the Dashboard."),
                QStringLiteral("Copy it. The free tier includes 1,000 credits/month (5,000 in the first 7 days)."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },
        {
            QStringLiteral("scrape_do"),
            QStringLiteral("Scrape.do"),
            QStringLiteral("Scrape.do"),
            QStringLiteral("Keyword / general"),
            QStringLiteral("SERP API for text queries. Second stage only."),
            QStringLiteral("https://scrape.do/"),
            true,
            QStringLiteral("scrape_do"),
            {
                QStringLiteral("Go to scrape.do and click Sign up."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("Open the Dashboard and find \"Your token\" / the API token."),
                QStringLiteral("Copy it. The free plan includes 1,000 credits/month."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },
        {
            QStringLiteral("zenrows"),
            QStringLiteral("ZenRows"),
            QStringLiteral("ZenRows"),
            QStringLiteral("Keyword / general"),
            QStringLiteral("Anti-bot scraping and SERP. Text search only."),
            QStringLiteral("https://www.zenrows.com/"),
            true,
            QStringLiteral("zenrows"),
            {
                QStringLiteral("Go to zenrows.com and click Sign up."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("On the Dashboard, copy the API Key shown at the top."),
                QStringLiteral("The free plan includes 5,000 credits/month."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },
        {
            QStringLiteral("outscraper"),
            QStringLiteral("Outscraper"),
            QStringLiteral("Outscraper"),
            QStringLiteral("Keyword / general"),
            QStringLiteral("Google Images by text query. Second stage only."),
            QStringLiteral("https://outscraper.com/"),
            true,
            QStringLiteral("outscraper"),
            {
                QStringLiteral("Go to outscraper.com and click Sign up."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("Open the Dashboard and go to the API section."),
                QStringLiteral("Copy your API key. The free tier gives 500 records per service every 30 days."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },
        {
            QStringLiteral("serpstack"),
            QStringLiteral("Serpstack"),
            QStringLiteral("Serpstack"),
            QStringLiteral("Keyword / general"),
            QStringLiteral("Google SERP as JSON. Text search only."),
            QStringLiteral("https://serpstack.com/signup/free"),
            true,
            QStringLiteral("serpstack"),
            {
                QStringLiteral("Go to serpstack.com and click Sign up (Free)."),
                QStringLiteral("Register with your email, confirm it, then log in."),
                QStringLiteral("Open the Dashboard and copy your API access key."),
                QStringLiteral("The free plan includes 100 requests/month."),
                QStringLiteral("Paste it here and click Validate."),
            },
        },
    };
    // Clang-format on
    return list;
}

const Provider* providerById(const QString& id)
{
    for (const Provider& p : providers()) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

}  // namespace app
