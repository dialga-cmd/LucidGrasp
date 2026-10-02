# Image — Surface Scan Plan

Last updated: 2026-10-03
Status: research + design complete; **key-entry platform implemented** on
`feature/surface-search` (storage, validation, local server, page). Provider
search calls and the image→keyword bridge are not written yet. See §10.

Supersedes the earlier `lists.md` (image-hosting + keyword-search list). This file is
scoped to the **surface scan** feature only: given an image the user has selected, find
where that image (or a visually similar one) appears across the surface web, and add it.

> Scope note: image **hosting / upload** providers (ImgBB, Catbox, Imgur, Flickr, …)
> are a separate concern and are not part of this plan.

> **Provider rule (decided):** only platforms that are **self-serve from their own website**
> *and* grant **recurring (monthly / daily / hourly) credits** are supported. One-time
> signup trials, sales-led / KYC-gated providers, and platforms with no API are excluded.
> See §9 for the kept list and the excluded list.

---

## 0. What the feature does

Two request shapes — keep them distinct:

- **Reverse image search** — input is an *image*; output is pages / URLs where it appears.
  This is the surface scan.
- **Keyword image search** — input is *text*; output is images. Not a scan of the user's
  chosen image; used only as a second stage after the **image → keyword bridge** (§5).

Decided model (from earlier sessions):

- **BYO key.** The user obtains their own API key, pastes it into a local page served on
  `127.0.0.1`, validates it, and syncs. The app ships **no** secret.
- Because the key is the user's, there is **no global rate-limit policy**. Still handle
  `429` / `Retry-After` and cache results — those are error handling and quota-saving, not
  quota-sharing.
- A keyless / free crawler can be used as a **verification layer**, not as a discovery
  engine (see §3).

---

## 1. Official reverse-image-search platforms — USE THESE

These are the platforms the software should document and integrate first. They are
real, official APIs (not scrapers), and they fit the BYO-key model.

| Platform | API | Auth | Input | Free tier | Paid |
|---|---|---|---|---|---|
| **Google Cloud Vision — Web Detection** | `POST https://vision.googleapis.com/v1/images:annotate` (`WEB_DETECTION`) | GCP API key or service-account OAuth2 | Image (base64 / GCS / URL) | **First 1,000 units/month free** | **$3.50 / 1,000 units** (units 1,001–5,000,000); above 5M contact Google |
| **SauceNAO** | `POST https://saucenao.com/search.php?output_type=2` | API key (free account; `api_key`) | Image URL or upload | Free key; short-window request limits | Free |
| **trace.moe** | `POST https://api.trace.moe/search` | None (anonymous) or `x-trace-key` | Image URL / base64 / upload | **~1,000 searches/month per IP** anonymous | Optional supporter key raises quota |

### Google Cloud Vision — Web Detection
- Returns: `fullMatchingImages`, `partialMatchingImages`, `pagesWithMatchingImages`,
  `visuallySimilarImages`, `webEntities`, `bestGuessLabels`.
- **This is not Google Lens.** Lens has no public API; Vision Web Detection is the closest
  official capability and its results are sparser than the Lens UI.
- Billing unit = **one feature applied to one image**. A single `WEB_DETECTION` request is
  1 unit, so 1,000 free images/month, then $3.50/1,000.
- Quotas are per GCP project and can be raised; the free 1,000/month is the practical cap
  for a no-billing key.
- Verdict: **usable** — official, general web-wide, BYO GCP key. Needs a billing account
  to exceed 1,000/month.

### SauceNAO
- Purpose: reverse-search **art / anime** images against known source databases (Pixiv,
  Danbooru, Sankaku, …). Strong for artwork provenance, irrelevant for ordinary photos.
- Auth: free account → API key. JSON output with a per-index similarity score.
- Short-window request limits are published per account tier; cache aggressively.
- Verdict: **usable** — free and official; domain-limited to art/anime.

### trace.moe
- Purpose: identify the **anime scene** a screenshot came from — title, episode, timestamp.
- Auth: none for anonymous use (rate-limited per IP, ~1,000 searches/month); an optional
  supporter `x-trace-key` raises the quota.
- Returns: AniList/MyAnimeList IDs, episode, `from`/`to` timestamps, similarity, preview.
- **Implementation note:** `/search` is **POST-only** (a `GET` returns `405`), so the
  key-entry page probes `GET /me` instead — anonymous-safe, returns the quota, and an
  invalid optional `x-trace-key` returns `403`.
- Verdict: **usable** — free and official; anime only.

---

## 2. Retired / not available — do not plan around these

These are documented as reference only (they are not providers we integrate). None has a
public reverse-image API and none grants recurring credits.

| Service | Reverse-image API status |
|---|---|
| **Bing Visual Search** | **Retired 2025-08-11** with the whole Bing Search API family (Web/Image/Video/News/Entity/Autosuggest/SpellCheck/Visual/Custom). Existing keys return `410 Gone`. |
| **Google Lens** | **No official public API.** Anything advertising a "Google Lens API" is scraping Google. |
| **Google Reverse Image** | Consumer web tool only; no endpoint. Use Cloud Vision Web Detection instead. |
| **Yandex** | **No official reverse-image API.** Only scrapeable (see §3). |
| **IQDB** | Anime/art reverse search, but **no official API** — web form only. |
| **Google Custom Search JSON** | **Closed to new customers**; existing use ends **2027-01-01** → migrate to Vertex AI Search. |

Microsoft's own replacement, "Grounding with Bing Search" (Azure AI Agents), is for
text grounding and does **not** do reverse image search.

---

## 3. Yandex and the crawler path

Yandex has **no official reverse-image API**, but its CBIR feature is reachable as a web
page and is scrapeable:

- Base: `https://yandex.com/images/search` with `rpt=imageview`, then a `cbir_page`
  parameter selects the dataset:
  - `cbir_page=similar` → visually similar images
  - `cbir_page=sites` → every page where the image appears
  - `cbir_page=products` → shopping matches
- Anti-bot is aggressive. A direct request from the app is unreliable and ToS-grey; a
  scraping vendor with residential / stealth proxy handling is needed for reliability.
- The app already has the pieces to **verify** crawled results: `core::computePHash` /
  `hammingSimilarity` and `CvMatcher` (SSIM + ORB + histogram) in `src/core/`.
- Verdict: **possible but fragile.** Use only as an optional, explicitly-consented path.
  The crawler is a verifier/expander — it cannot discover URLs on its own; discovery must
  come from an API in §1 or §4.

---

## 4. Third-party platforms (self-serve, recurring credits)

None of these are official. They scrape the big engines and resell structured JSON. They
are usable with a BYO key, and the *vendor* carries the scraping/ToS exposure (SerpApi
sells a U.S. legal shield). Only vendors that meet the §9 provider rule are listed.

Note on cost: a "reverse lookup" is one request; some vendors bundle ~100 image results
per request, so per-1K-image and per-1K-request figures differ.

| Platform | Reverse endpoint / wraps | Auth | Free tier (recurring) | Paid entry | Usable in app? |
|---|---|---|---|---|---|
| **SerpApi** | Google Lens, Google Reverse Image, Google Images, Yandex Reverse Image, Bing, 80+ engines | `api_key` | **250 searches/mo** (50/hr throughput) | $25/mo 1,000; $75/5,000; $275/30,000 | **Yes** — widest coverage incl. Yandex reverse |
| **Zenserp** | Google **Reverse Image Search** + Google/Bing/Yandex search | `apikey` | **50/mo** | From ~$24–$50/mo (5,000–25,000) | **Yes** — explicit reverse endpoint; tiny free tier |
| **HasData** | Google Images API (text) | API key | **1,000 credits/mo** | ~$0.35–$0.50/1K | Keyword-bridge only (text search) |
| **ScraperAPI** | General scraping + Google Images (no Lens/reverse) | `api_key` | **1,000/mo** (+5,000 first 7 days) | $49/mo 100,000 credits | Keyword-bridge only (no Lens) |
| **Scrape.do** | SERP API (no reverse) | API key | **1,000 credits/mo** | ~$1.16/1K | Keyword-bridge only |
| **ZenRows** | SERP / general scraping (no reverse) | API key | **5,000 credits/mo** | $16/mo 45K | Keyword-bridge only |
| **Outscraper** | Google Images (no-code, text) | API key | **500 records/service per rolling 30 days** | $3/1K records | Keyword-bridge only |
| **Serpstack** | Google SERP | API key | **100 requests/mo** | $29.99/mo | Keyword-bridge only |

Providers dropped by the recurring-credit rule (one-time trial only) and the gated
platforms are recorded in §9.2.

---

## 5. Platform matrix (supported platforms only)

Strategy note first, because it governs the "Keyword handling" column:

**Image → keyword bridge.** For platforms that only do text search, run the selected image
through a reverse-capable provider first (Cloud Vision `bestGuessLabels` / `webEntities`,
SauceNAO titles, trace.moe titles) or a local caption/label model, then feed those terms
into the keyword API. Keyword platforms are therefore *second-stage* providers in the same
flow, not standalone surface-scan sources.

`b64` = base64 image; `→` = response shape. Endpoints are the documented request shapes at
research time — verify exact paths and auth before coding.

| Platform | Category | API structure | Keyword handling | Recurring free credits |
|---|---|---|---|---|
| **Google Cloud Vision** (Web Detection) | Official reverse | `POST vision.googleapis.com/v1/images:annotate?key=KEY` · body `{"requests":[{"image":{"content":b64},"features":[{"type":"WEB_DETECTION"}]}]}` → `webDetection.{fullMatchingImages,partialMatchingImages,pagesWithMatchingImages,visuallySimilarImages,webEntities,bestGuessLabels}` | Native reverse; its labels feed keyword APIs | 1,000 units/mo |
| **SauceNAO** | Official reverse (art/anime) | `POST saucenao.com/search.php` multipart · `api_key`, `output_type=2`, `file`\|`url`, `db`, `numres` → `results[].header.similarity` + `data.*` | Native; result titles → keywords | Free key (short-window limits) |
| **trace.moe** | Official reverse (anime) | `POST api.trace.moe/search?url=…` (or file) · optional `X-Trace-Key` → `{result:[{anilist,mal,episode,from,to,similarity,video,image}]}` | Native anime scene | ~1,000/mo per IP |
| **SerpApi** | Third-party reverse | `GET serpapi.com/search?engine=google_lens&url=<img>&api_key=…` · engines `google_lens`, `google_reverse_image`, `yandex_images`, `google_images` → `visual_matches[]`,`exact_matches[]` | `engine=google_images` | 250/mo |
| **Zenserp** | Third-party reverse | `GET app.zenserp.com/api/v2/search?apikey=&search_type=image` (+ reverse image param) → JSON | `search_type=image` | 50/mo |
| **HasData** | Keyword only | `GET api.hasdata.com/scrape/google/images?q=` (`x-api-key`) → JSON | Image→keywords, then query | 1,000 credits/mo |
| **ScraperAPI** | Keyword/general | `GET api.scraperapi.com/?api_key=&url=<Google Images URL>&render=true` → HTML | Image→keywords, then scrape | 1,000/mo |
| **Scrape.do** | Keyword/general | `GET api.scrape.do/?token=&url=` → SERP | keyword SERP | 1,000/mo |
| **ZenRows** | Keyword/general | `GET /?apikey=&url=&mode=auto` → HTML/JSON | keyword SERP | 5,000/mo |
| **Outscraper** | Keyword only | `GET api.app.outscraper.com/google-images?query=` (`X-API-KEY`) | Image→keywords, then query | 500/service per 30 days |
| **Serpstack** | Keyword/general | `GET ?access_key=&query=` → JSON | keyword SERP | 100/mo |

### Reading the matrix
- **Reverse-native** (Cloud Vision, SauceNAO, trace.moe, SerpApi, Zenserp): input is the
  image directly.
- **Keyword rows** (bottom): not usable alone for a surface scan; only useful after the
  image→keyword bridge above.
- **No-API engines** (Lens, Google Reverse Image, Bing, Yandex CBIR, IQDB) are documented
  in §2 as "do not plan around"; the crawler path is §3.

---

## 6. Recommended set

1. **Documented / first-class (official):**
   - **Google Cloud Vision — Web Detection** (general web, 1,000 free/mo then $3.50/1K).
   - **SauceNAO** (art/anime sources, free).
   - **trace.moe** (anime scenes, free).
2. **Optional bring-your-own-key reverse wrappers (unofficial):**
   - **SerpApi** (broadest, includes Yandex reverse), **Zenserp** (explicit reverse
     endpoint, cheap entry).
3. **Crawler path:** Yandex CBIR, explicitly consented, verified locally with
   `CvMatcher` / pHash — never the sole discovery source.
4. **Keyword-bridge providers (recurring credits):** SerpApi, Zenserp, HasData,
   ScraperAPI, Scrape.do, ZenRows, Outscraper, Serpstack — used only after the
   image→keyword bridge.
5. **Excluded:** all one-time-trial-only and gated platforms (§9.2), and everything in
   §2.

---

## 7. Integration model (reference)

- User selects **Internet – Surface scan** → app starts a loopback HTTP server on
  `127.0.0.1` (OS-assigned ephemeral port, one-time token in the URL, `Host`/`Origin`
  checks) → opens `http://127.0.0.1:<port>/?token=<token>` in the default browser.
- Page lists providers (official first), each with a key field + **Validate**. Validation
  runs server-side in C++ and calls the provider — never from browser JS.
- **Sync** commits every non-empty key to layered storage
  (`environment > OS keychain > owner-only INI`) and enables those providers. Keys persist
  across tab close / restart; the page re-reads them (masked) on every open. The page
  itself stores nothing.
- App ships **no** secret; only the `SecretStore`/provider code is committed. Add `.env`,
  `*.key`, `providers.ini` to `.gitignore`. Never log keys (ImgBB-style query params must
  be redacted).
- Keep `429` + `Retry-After` backoff and a local result cache (SHA-256 dedupe) even though
  there is no global rate limit.

---

## 8. Open questions / to verify

- Google Cloud Vision: confirm Web Detection quality vs. Lens for the app's image types,
  and whether a no-billing key's 1,000/month is enough; confirm current GCP quota defaults.
- SauceNAO / trace.moe: confirm current key issuance and per-account rate limits, and that
  both are acceptable for a desktop client's BYO-key use.
- Yandex: confirm CBIR still works through a stealth/residential scraping vendor and
  current credit cost at build time; the crawler stays verifier-only.
- Kept vendors (SerpApi, Zenserp, HasData, ScraperAPI, Scrape.do, ZenRows, Outscraper,
  Serpstack): re-check free tiers and prices at implementation time (they move), and read
  each vendor's ToS on desktop / "bring-your-own-key" redistribution.
- Reconsider if the recurring-credit rule is relaxed: the excluded one-time-trial
  providers in §9.2 (Serper.dev, ScrapingBee, Scrapingdog, SearchApi.io, Oxylabs, Apify,
  ScrapeBadger, DataForSEO, Zyte, WebScrapingAPI, Serpent, Scrapfly, Decodo) could return.
- Decide the final ordered provider list and fallback chain (official → reverse wrappers →
  keyword bridge → crawler).

---

## 9. How to obtain each API — (verified Oct 2026)

Rule: self-serve from the vendor's website **and** recurring (monthly / daily / hourly)
credits. 11 platforms qualify.

### 9.1 Kept — self-serve with recurring credits (11)

| Platform | Where to get the key | Recurring free credits | Card / KYC |
|---|---|---|---|
| Google Cloud Vision | GCP Console → enable Cloud Vision API → Credentials | 1,000 units/mo | Card (billing account) |
| SauceNAO | `saucenao.com` → user.php?page=search-api | Free API for registered users (short-window limits) | No |
| trace.moe | No key needed; optional donor key at `trace.moe/account` | ~1,000 searches/mo per IP | No |
| SerpApi | `serpapi.com` → register → dashboard | 250 searches/mo | Card for free plan (anti-fraud) |
| Zenserp | `zenserp.com` → Get API Key | 50 searches/mo | No |
| HasData | `hasdata.com` → sign up | 1,000 credits/mo | No |
| ScraperAPI | `scraperapi.com` → sign up | 1,000 credits/mo (+5,000 first 7 days) | No |
| Scrape.do | `scrape.do` → sign up | 1,000 credits/mo | No |
| ZenRows | `zenrows.com` → sign up | 5,000 credits/mo | No |
| Outscraper | `outscraper.com` → sign up | 500 records/service per rolling 30 days | No |
| Serpstack | `serpstack.com` → sign up | 100 requests/mo | No |

### 9.2 Excluded — one-time trial only, or gated (not used)

Excluded by the recurring-credit rule (their free allowance is a one-time signup/trial
balance, not a monthly/daily/hourly reset), or because they are sales/KYC-gated:

| Platform | Why excluded |
|---|---|
| Serper.dev | 2,500 free credits are a one-time allowance (expire 6 months); no monthly reset |
| Scrapingdog | 30-day / one-time signup credits |
| SearchApi.io | 100 free requests one-time |
| ScrapingBee | 1,000 one-time trial credits |
| Oxylabs | One-time free trial (no recurring credits) |
| Apify | Removed by request |
| ScrapeBadger | 1,000 free credits on signup (one-time); PAYG never expires but no recurring reset |
| DataForSEO | $1 one-time trial credit; $50 min top-up |
| Zyte | $5 one-time credit, no permanent free tier |
| WebScrapingAPI | 5,000 one-time trial (no confirmed recurring free tier) |
| Serpent | 10 free searches on signup (one-time) |
| Scrapfly | 1,000 one-time credits (no expiry); removed by request |
| Decodo | Free trial only; removed by request |
| Yandex AI Studio Search | Self-serve but paid; no recurring free credits |
| Bright Data | Gated — new Residential zones after 2026-07-07 require company KYC |
| Traject Data | Gated — sales-led, from $125/mo |

### 9.3 Not obtainable — no API / retired (reference)

| Service | Why |
|---|---|
| Google Lens | No official API; only scraped by third parties |
| Google Reverse Image | No API (superseded by Lens) |
| Bing Visual Search | Retired 2025-08-11; keys return `410 Gone` |
| Yandex CBIR | No official reverse-image API; scrape only |
| IQDB | No API; scrape only |
| Google Custom Search JSON | **Closed to new customers**; existing use ends 2027-01-01 → migrate to Vertex AI Search |

---

## 10. Implementation status (2026-10-03)

The **key-entry platform** is implemented on `feature/surface-search`; the search
itself is not.

Built:

- `SecretStore` — layered key storage, precedence `environment > OS keychain >
  owner-only INI` (`0600`), plus a mask that shows only the tail. The browser never
  receives a key, only the mask.
- `providers.cpp` — the eleven kept providers (§9.1) as pure data, each with display
  name, category, description, signup URL and a step-by-step signup walkthrough.
- `KeyEntryServer` — loopback HTTP server on an OS-assigned port. Per-session random
  token, `Host` check, `Origin` check, length-independent token compare. Endpoints:
  `GET /api/state`, `POST /api/validate`, `POST /api/sync`, `POST /api/remove`.
- `key_entry_page.h` — the whole page (grid of self-contained provider cards,
  light/dark, "How to get a key" tutorial) as one embedded string with no external
  requests (`CSP: default-src 'none'`).
- `KeyEntryDialog` — asks permission before starting the server, shows a busy state,
  opens the browser exactly once, and reuses itself on a second tap.
- Self-test Case 10 covers the store, the mask, the registry, the request parser, the
  token compare and the host check.

Validation probes (one-shot, server-side in C++):

| Provider | Probe |
|---|---|
| Google Cloud Vision | `images:annotate` with a 1×1 PNG, `LABEL_DETECTION` (1 unit) |
| SauceNAO | `search.php` |
| trace.moe | `GET /me` (keyless-safe); optional `x-trace-key` — invalid key → `403` |
| SerpApi | `/account` |
| Zenserp | `/api/v2/status` |
| HasData | `x-api-key` account call |
| ScraperAPI | `/account` |
| Scrape.do | `/info` |
| ZenRows | `v1/?apikey=&url=` |
| Outscraper | `X-API-KEY` → `/profile` |
| Serpstack | `/account` |

Not built:

- Provider `search(image)` calls that turn a selected image into results.
- The image→keyword bridge (§5) that feeds the keyword-only vendors.
- Result caching and `429` / `Retry-After` backoff (planned, §7).

ZenRows and Scrape.do are the least certain probes and would be the first to need
adjusting if a vendor changes its API.
