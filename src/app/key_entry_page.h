#pragma once

namespace app {

// Full HTML for the key-entry page, kept in its own translation-free header so
// the page can be edited without touching the server logic. The session token
// is read from the page's own URL, never injected here, so this string is
// byte-identical for every session.
//
// Visual system (kept intentionally small):
//   - cool neutral surfaces + ONE accent (emerald); success/attention are the
//     accent, errors are the only other hue
//   - radius scale: cards/modal 14px, controls 9px, pills full
//   - system font stack: the page's CSP allows no external fonts, so a good
//     native stack is the honest choice, with a mono stack for key material
//   - light and dark are both first-class; explicit toggle overrides system
//     preference, which is remembered locally (never keys or usage data)
inline const char* keyEntryPageHtml()
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>Surface scan — API keys · LucidGrasp</title>
<style>
  :root {
    color-scheme: light dark;
    --bg: #f4f5f7;
    --surface: #ffffff;
    --surface-2: #eef0f3;
    --surface-3: #e7eaee;
    --text: #15181d;
    --muted: #5a6472;
    --faint: #69727f;
    --border: #e3e6ea;
    --border-strong: #d0d5dc;
    --accent: #0b7a5e;
    --accent-hover: #096249;
    --accent-soft: #e5f3ee;
    --on-accent: #ffffff;
    --danger: #b42318;
    --danger-soft: #fbebe9;
    --ring: rgba(14, 143, 111, .28);
    --shadow: 0 1px 2px rgba(16, 24, 40, .04), 0 6px 16px -6px rgba(16, 24, 40, .10);
    --shadow-lg: 0 24px 64px -24px rgba(16, 24, 40, .34);
    --r-lg: 14px;
    --r-md: 9px;
    --r-pill: 999px;
    --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    --mono: ui-monospace, SFMono-Regular, "SF Mono", "JetBrains Mono", Menlo, Consolas, monospace;
  }
  [data-theme="dark"] {
    --bg: #0d1013;
    --surface: #14181c;
    --surface-2: #1a1f24;
    --surface-3: #212730;
    --text: #e8ebef;
    --muted: #9aa4b0;
    --faint: #8a94a2;
    --border: #242a31;
    --border-strong: #333b44;
    --accent: #35cf9f;
    --accent-hover: #4fdcae;
    --accent-soft: #102a23;
    --on-accent: #04140f;
    --danger: #f97066;
    --danger-soft: #2a1512;
    --ring: rgba(53, 207, 159, .30);
    --shadow: 0 1px 2px rgba(0, 0, 0, .5);
    --shadow-lg: 0 30px 80px -30px rgba(0, 0, 0, .8);
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg: #0d1013; --surface: #14181c; --surface-2: #1a1f24; --surface-3: #212730;
      --text: #e8ebef; --muted: #9aa4b0; --faint: #707b88;
      --border: #242a31; --border-strong: #333b44;
      --accent: #35cf9f; --accent-hover: #4fdcae; --accent-soft: #102a23;
      --on-accent: #04140f; --danger: #f97066; --danger-soft: #2a1512;
      --ring: rgba(53, 207, 159, .30);
      --shadow: 0 1px 2px rgba(0, 0, 0, .5);
      --shadow-lg: 0 30px 80px -30px rgba(0, 0, 0, .8);
    }
  }

  * { box-sizing: border-box; }
  html, body { height: 100%; }
  body {
    margin: 0; background: var(--bg); color: var(--text);
    font-family: var(--sans); font-size: 14px; line-height: 1.5;
    -webkit-font-smoothing: antialiased; text-rendering: optimizeLegibility;
  }
  button, input { font-family: inherit; }
  ::selection { background: var(--accent-soft); color: var(--text); }

  /* ---- top bar ---- */
  .topbar {
    position: sticky; top: 0; z-index: 30;
    display: flex; align-items: center; justify-content: space-between; gap: 16px;
    padding: 14px 28px; background: var(--bg); border-bottom: 1px solid var(--border);
  }
  .brand { display: flex; align-items: center; gap: 12px; min-width: 0; }
  .mark {
    display: grid; place-items: center; width: 34px; height: 34px; flex: none;
    border-radius: 10px; background: var(--surface); color: var(--accent);
    border: 1px solid var(--border); box-shadow: var(--shadow);
  }
  .brand h1 { margin: 0; font-size: 15px; font-weight: 620; letter-spacing: -.01em; }
  .brand p { margin: 1px 0 0; font-size: 12.5px; color: var(--muted); }
  .topbar-actions { display: flex; align-items: center; gap: 10px; }
  .status { display: inline-flex; align-items: center; gap: 7px; font-size: 12.5px;
            color: var(--muted); padding: 0 4px; white-space: nowrap; }
  .status .sdot { width: 7px; height: 7px; border-radius: 50%; background: var(--faint);
                  transition: background .2s ease; }
  .status.on { color: var(--text); }
  .status.on .sdot { background: var(--accent); box-shadow: 0 0 0 3px var(--accent-soft); }

  /* ---- buttons ---- */
  .btn {
    display: inline-flex; align-items: center; justify-content: center; gap: 7px;
    height: 34px; padding: 0 13px; border-radius: var(--r-md);
    border: 1px solid transparent; background: transparent; color: var(--text);
    font-size: 13px; font-weight: 560; letter-spacing: -.005em; cursor: pointer;
    white-space: nowrap; transition: background .15s ease, border-color .15s ease,
                                 color .15s ease, transform .06s ease;
  }
  .btn:active { transform: translateY(1px); }
  .btn:focus-visible { outline: none; box-shadow: 0 0 0 3px var(--ring); }
  .btn.sm { height: 32px; padding: 0 11px; font-size: 12.5px; }
  .btn.primary { background: var(--accent); color: var(--on-accent); }
  .btn.primary:hover { background: var(--accent-hover); }
  .btn.ghost { border-color: var(--border); background: var(--surface); }
  .btn.ghost:hover { background: var(--surface-2); border-color: var(--border-strong); }
  .btn[disabled] { opacity: .5; cursor: default; transform: none; }
  .icon-btn {
    display: grid; place-items: center; width: 34px; height: 34px; flex: none;
    border-radius: var(--r-md); border: 1px solid var(--border);
    background: var(--surface); color: var(--muted); cursor: pointer;
    transition: background .15s ease, color .15s ease, border-color .15s ease;
  }
  .icon-btn:hover { background: var(--surface-2); color: var(--text); }
  .icon-btn:focus-visible { outline: none; box-shadow: 0 0 0 3px var(--ring); }
  .icon-btn svg { width: 17px; height: 17px; }
  .i-moon { display: none; }
  [data-theme="dark"] .i-sun { display: none; }
  [data-theme="dark"] .i-moon { display: block; }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) .i-sun { display: none; }
    :root:not([data-theme="light"]) .i-moon { display: block; }
  }

  /* ---- banner ---- */
  .banner {
    display: flex; align-items: flex-start; gap: 9px;
    margin: 16px auto -4px; max-width: 1240px; width: calc(100% - 56px);
    padding: 11px 14px; border: 1px solid var(--border); border-radius: var(--r-md);
    background: var(--surface); color: var(--text); font-size: 13px;
  }
  .banner.good { border-color: color-mix(in srgb, var(--accent) 40%, var(--border));
                 background: var(--accent-soft); }
  .banner.bad { border-color: color-mix(in srgb, var(--danger) 40%, var(--border));
                background: var(--danger-soft); }
  .banner svg { width: 16px; height: 16px; flex: none; margin-top: 1px; }

  /* ---- grid + cards ---- */
  .wrap { max-width: 1240px; margin: 0 auto; padding: 22px 28px 8px; }
  .grid { display: grid; gap: 16px;
          grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); }

  .card {
    display: flex; flex-direction: column; gap: 12px;
    padding: 16px; background: var(--surface); border: 1px solid var(--border);
    border-radius: var(--r-lg); box-shadow: var(--shadow);
    transition: border-color .15s ease, box-shadow .15s ease;
  }
  .card:hover { border-color: var(--border-strong); }
  .card-top { display: flex; align-items: center; gap: 11px; }
  .badge {
    display: grid; place-items: center; width: 36px; height: 36px; flex: none;
    border-radius: 10px; background: var(--surface-2); border: 1px solid var(--border);
    color: var(--text); font-family: var(--mono); font-size: 12px; font-weight: 600;
    letter-spacing: .02em;
  }
  .card-title { min-width: 0; flex: 1 1 auto; }
  .card-title h2 { margin: 0; font-size: 13.5px; font-weight: 620; letter-spacing: -.01em;
                   white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .cat { display: block; margin-top: 2px; font-size: 10.5px; font-weight: 560;
         letter-spacing: .05em; text-transform: uppercase; color: var(--faint); }
  .dot { width: 8px; height: 8px; border-radius: 50%; flex: none;
         background: var(--border-strong); transition: background .2s ease, box-shadow .2s ease; }
  .dot.on { background: var(--accent); box-shadow: 0 0 0 3px var(--accent-soft); }
  .desc { margin: 0; font-size: 12.5px; line-height: 1.5; color: var(--muted);
          display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical;
          overflow: hidden; }

  .field { display: flex; flex-direction: column; gap: 6px; }
  .lab { font-size: 11.5px; font-weight: 560; color: var(--muted); }
  .field input {
    width: 100%; height: 36px; padding: 0 11px; border-radius: var(--r-md);
    border: 1px solid var(--border); background: var(--surface-2); color: var(--text);
    font-family: var(--mono); font-size: 12.5px;
    transition: border-color .15s ease, box-shadow .15s ease, background .15s ease;
  }
  .field input::placeholder { color: var(--muted); }
  .field input:focus { outline: none; border-color: var(--accent);
                       background: var(--surface); box-shadow: 0 0 0 3px var(--ring); }

  .actions { display: flex; gap: 8px; }
  .actions .btn { flex: 1 1 0; }
  .result { margin: 0; min-height: 15px; font-size: 12px; line-height: 1.4; color: var(--faint);
            overflow-wrap: anywhere; }
  .result.ok { color: var(--accent); }
  .result.bad { color: var(--danger); }
  .linkbtn {
    align-self: flex-start; margin-top: -2px; padding: 0; border: none; background: none;
    color: var(--faint); font-size: 11.5px; cursor: pointer;
  }
  .linkbtn:hover { color: var(--danger); text-decoration: underline; }
  .linkbtn:focus-visible { outline: none; box-shadow: 0 0 0 3px var(--ring); border-radius: 4px; }

  /* ---- skeleton ---- */
  .sk { display: flex; flex-direction: column; gap: 12px; padding: 16px;
        background: var(--surface); border: 1px solid var(--border);
        border-radius: var(--r-lg); box-shadow: var(--shadow); }
  .sk > * { border-radius: 8px; background: var(--surface-3); }
  .sk .row { display: flex; align-items: center; gap: 11px; }
  .sk .sq { width: 36px; height: 36px; flex: none; }
  .sk .ln { height: 12px; }
  .sk .ln.w60 { width: 60%; } .sk .ln.w40 { width: 40%; }
  .sk .bar { height: 36px; }
  @media (prefers-reduced-motion: no-preference) {
    .sk > * { animation: pulse 1.5s ease-in-out infinite; }
    @keyframes pulse { 0%, 100% { opacity: .55 } 50% { opacity: .95 } }
  }

  /* ---- footer ---- */
  .foot { max-width: 1240px; margin: 0 auto; padding: 18px 28px 40px;
          color: var(--faint); font-size: 12px; line-height: 1.6; }
  .foot strong { color: var(--muted); font-weight: 560; }

  /* ---- modal ---- */
  .modal { position: fixed; inset: 0; z-index: 50; display: grid; place-items: center;
           padding: 24px; background: rgba(7, 10, 14, .58); }
  .modal[hidden] { display: none; }
  .sheet {
    width: min(640px, 100%); max-height: 86vh; overflow: auto;
    background: var(--surface); border: 1px solid var(--border);
    border-radius: var(--r-lg); box-shadow: var(--shadow-lg); padding: 24px;
  }
  .sheet-head { display: flex; align-items: center; gap: 12px; }
  .sheet-head h3 { margin: 0; font-size: 16px; font-weight: 640; letter-spacing: -.01em; }
  .sheet-head .cat { margin-top: 3px; }
  .sheet .sub { margin: 10px 0 0; font-size: 12.5px; color: var(--muted); }
  ol.steps { list-style: none; counter-reset: step; margin: 18px 0 0; padding: 0;
             display: flex; flex-direction: column; gap: 13px; }
  ol.steps li { counter-increment: step; display: grid; grid-template-columns: 24px 1fr;
                gap: 12px; font-size: 13.5px; line-height: 1.55; color: var(--text); }
  ol.steps li::before {
    content: counter(step); display: grid; place-items: center; width: 24px; height: 24px;
    border-radius: var(--r-pill); background: var(--surface-2); border: 1px solid var(--border);
    color: var(--muted); font-family: var(--mono); font-size: 11.5px; font-weight: 600;
  }
  .sheet-actions { display: flex; align-items: center; justify-content: space-between;
                   gap: 10px; margin-top: 22px; }
  .sheet-actions .btn.ghost { text-decoration: none; }

  @media (max-width: 620px) {
    .topbar { padding: 12px 16px; } .wrap { padding: 18px 16px 8px; }
    .banner, .foot { width: auto; margin-left: 16px; margin-right: 16px; padding-left: 16px; }
    .foot { padding-left: 16px; padding-right: 16px; }
    .brand p { display: none; }
  }
</style>
</head>
<body>
<header class="topbar">
  <div class="brand">
    <span class="mark" aria-hidden="true">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor"
           stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">
        <path d="M4 8V6.5A2.5 2.5 0 0 1 6.5 4H8"/>
        <path d="M16 4h1.5A2.5 2.5 0 0 1 20 6.5V8"/>
        <path d="M20 16v1.5a2.5 2.5 0 0 1-2.5 2.5H16"/>
        <path d="M8 20H6.5A2.5 2.5 0 0 1 4 17.5V16"/>
        <circle cx="12" cy="12" r="2.6"/>
      </svg>
    </span>
    <div>
      <h1>Surface scan</h1>
      <p>Connect your own API keys to search where an image appears</p>
    </div>
  </div>
  <div class="topbar-actions">
    <span class="status" id="status"><span class="sdot"></span><span id="status-text">Loading…</span></span>
    <button class="icon-btn" id="theme" type="button" title="Toggle light and dark"
            aria-label="Toggle colour theme">
      <svg class="i-sun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"
           stroke-linecap="round" stroke-linejoin="round">
        <circle cx="12" cy="12" r="4"/>
        <path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>
      </svg>
      <svg class="i-moon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"
           stroke-linecap="round" stroke-linejoin="round">
        <path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>
      </svg>
    </button>
    <button class="btn primary" id="sync" type="button">Sync</button>
  </div>
</header>

<div class="banner" id="banner" role="status" hidden></div>

<main class="wrap">
  <div class="grid" id="grid"></div>
</main>

<footer class="foot">
  <strong>Where keys go:</strong> the app stores them in your OS keychain when one is
  available, otherwise in an owner-only settings file. This page never keeps a key and
  sends it only to the local app for validation. Your light/dark choice is the only thing
  remembered here, in this browser.
</footer>

<div class="modal" id="modal" hidden>
  <div class="sheet" role="dialog" aria-modal="true" aria-labelledby="howto-title">
    <div class="sheet-head">
      <span class="badge" id="howto-badge"></span>
      <div>
        <h3 id="howto-title"></h3>
        <span class="cat" id="howto-cat"></span>
      </div>
    </div>
    <p class="sub" id="howto-sub"></p>
    <ol class="steps" id="howto-steps"></ol>
    <div class="sheet-actions">
      <a class="btn ghost" id="howto-link" target="_blank" rel="noreferrer noopener">Open the site ↗</a>
      <button class="btn primary" id="howto-close" type="button">Got it</button>
    </div>
  </div>
</div>

<script>
(function () {
  "use strict";

  var TOKEN = new URLSearchParams(location.search).get("token") || "";
  var state = null;

  /* ---- theme (the only thing stored locally, and never a key) ---- */
  var root = document.documentElement;
  try {
    var savedTheme = localStorage.getItem("lucidgrasp-theme");
    if (savedTheme === "light" || savedTheme === "dark") root.dataset.theme = savedTheme;
  } catch (e) { /* storage blocked: fall back to the system preference */ }
  document.getElementById("theme").addEventListener("click", function () {
    var current = root.dataset.theme;
    if (current !== "light" && current !== "dark")
      current = window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
    var next = current === "dark" ? "light" : "dark";
    root.dataset.theme = next;
    try { localStorage.setItem("lucidgrasp-theme", next); } catch (e) {}
  });

  /* ---- helpers ---- */
  function api(path, body) {
    var options = { headers: { "X-LucidGrasp-Token": TOKEN } };
    if (body !== undefined) {
      options.method = "POST";
      options.headers["Content-Type"] = "application/json";
      options.body = JSON.stringify(body);
    }
    return fetch(path, options).then(function (response) {
      return response.json().catch(function () { return {}; })
        .then(function (json) { return { status: response.status, json: json }; });
    });
  }

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function initials(name) {
    var parts = String(name).split(/[^A-Za-z0-9]+/).filter(Boolean);
    var s = parts.length >= 2 ? parts[0].charAt(0) + parts[1].charAt(0)
                             : (parts[0] || String(name)).slice(0, 2);
    return s.toUpperCase();
  }

  function banner(text, kind) {
    var box = document.getElementById("banner");
    if (!text) { box.hidden = true; box.textContent = ""; return; }
    box.hidden = false;
    box.className = "banner" + (kind ? " " + kind : "");
    box.textContent = text;
  }

  function openHowto(provider) {
    document.getElementById("howto-badge").textContent = initials(provider.shortName || provider.name);
    document.getElementById("howto-title").textContent = provider.name;
    document.getElementById("howto-cat").textContent = provider.category;
    document.getElementById("howto-sub").textContent = provider.needsKey
      ? "Follow these steps in order. No prior experience with the platform is assumed."
      : "This provider works without a key. These steps are optional and only raise your quota.";
    var list = document.getElementById("howto-steps");
    list.textContent = "";
    (provider.steps || []).forEach(function (step) { list.appendChild(el("li", null, step)); });
    var link = document.getElementById("howto-link");
    if (provider.signupUrl) { link.href = provider.signupUrl; link.hidden = false; }
    else { link.hidden = true; }
    document.getElementById("modal").hidden = false;
    document.getElementById("howto-close").focus();
  }

  function closeHowto() { document.getElementById("modal").hidden = true; }

  /* ---- one self-contained card per provider ---- */
  function card(provider) {
    var box = el("article", "card");

    var top = el("div", "card-top");
    top.appendChild(el("span", "badge", initials(provider.shortName || provider.name)));
    var title = el("div", "card-title");
    title.appendChild(el("h2", null, provider.name));
    title.appendChild(el("span", "cat", provider.category));
    top.appendChild(title);
    var dot = el("span", "dot" + (provider.enabled ? " on" : ""));
    dot.title = provider.enabled ? "Ready" : "Not configured";
    dot.setAttribute("role", "img");
    dot.setAttribute("aria-label", dot.title);
    top.appendChild(dot);
    box.appendChild(top);

    box.appendChild(el("p", "desc", provider.description));

    var field = el("label", "field");
    field.appendChild(el("span", "lab", provider.needsKey ? "API key" : "API key (optional)"));
    var input = el("input");
    input.type = "password";
    input.autocomplete = "off";
    input.spellcheck = false;
    input.dataset.id = provider.id;
    input.placeholder = provider.hasKey
      ? provider.masked
      : (provider.needsKey ? "Paste your key" : "Add a donor key (optional)");
    if (provider.hasKey) input.title = "Saved in " + provider.source;
    field.appendChild(input);
    box.appendChild(field);

    var result = el("p", "result");
    result.setAttribute("role", "status");
    if (provider.hasKey) result.textContent = "Saved in " + provider.source + ".";
    else if (!provider.needsKey) result.textContent = "No key required.";

    var actions = el("div", "actions");
    var validate = el("button", "btn primary sm", provider.needsKey ? "Validate" : "Test");
    validate.type = "button";
    var howto = el("button", "btn ghost sm", "How to get a key");
    howto.type = "button";
    actions.appendChild(validate);
    actions.appendChild(howto);
    box.appendChild(actions);

    box.appendChild(result);

    if (provider.hasKey) {
      var remove = el("button", "linkbtn", "Remove saved key");
      remove.type = "button";
      remove.addEventListener("click", function () {
        remove.disabled = true;
        api("/api/remove", { id: provider.id }).then(function () {
          banner("Removed the saved key for " + provider.name + ".", "");
          refresh();
        });
      });
      box.appendChild(remove);
    }

    validate.addEventListener("click", function () {
      validate.disabled = true;
      result.className = "result";
      result.textContent = "Checking…";
      api("/api/validate", { id: provider.id, key: input.value }).then(function (r) {
        validate.disabled = false;
        var json = r.json || {};
        result.textContent = json.message || (json.ok ? "Looks good." : "Did not validate.");
        result.className = "result " + (json.ok ? "ok" : "bad");
      }).catch(function () {
        validate.disabled = false;
        result.textContent = "Lost contact with LucidGrasp.";
        result.className = "result bad";
      });
    });

    howto.addEventListener("click", function () { openHowto(provider); });
    return box;
  }

  function skeletons() {
    var grid = document.getElementById("grid");
    grid.textContent = "";
    for (var i = 0; i < 6; i++) {
      var sk = el("div", "sk");
      var row = el("div", "row");
      row.appendChild(el("span", "sq"));
      var lines = el("div");
      lines.style.flex = "1";
      lines.appendChild(el("div", "ln w60"));
      var gap = el("div"); gap.style.height = "8px"; lines.appendChild(gap);
      lines.appendChild(el("div", "ln w40"));
      row.appendChild(lines);
      sk.appendChild(row);
      sk.appendChild(el("div", "ln"));
      sk.appendChild(el("div", "bar"));
      grid.appendChild(sk);
    }
  }

  function render() {
    var grid = document.getElementById("grid");
    grid.textContent = "";
    state.providers.forEach(function (provider) { grid.appendChild(card(provider)); });

    var ready = state.providers.filter(function (p) { return p.enabled; }).length;
    var total = state.providers.length;
    var status = document.getElementById("status");
    document.getElementById("status-text").textContent = ready + " of " + total + " active";
    status.className = "status" + (state.searchEnabled ? " on" : "");
  }

  function refresh() {
    return api("/api/state", undefined).then(function (r) {
      if (r.status !== 200 || !r.json || !r.json.providers) {
        banner("Could not load provider state (HTTP " + r.status + ").", "bad");
        return;
      }
      state = r.json;
      render();
    }).catch(function () {
      banner("Lost contact with LucidGrasp. Is the app still running?", "bad");
    });
  }

  document.getElementById("sync").addEventListener("click", function () {
    var keys = {};
    document.querySelectorAll("input[data-id]").forEach(function (input) {
      var value = input.value.trim();
      if (value) keys[input.dataset.id] = value;
    });
    api("/api/sync", { keys: keys }).then(function (r) {
      var json = r.json || {};
      var saved = (json.saved || []).length;
      var failed = json.failed || [];
      if (failed.length) banner("Saved " + saved + " key" + (saved === 1 ? "" : "s")
                                + "; " + failed.length + " could not be stored.", "bad");
      else if (saved) banner("Saved " + saved + " key" + (saved === 1 ? "" : "s") + ". Surface scan is ready.", "good");
      else banner("Nothing to save yet. Paste a key, then Sync.", "");
      return refresh();
    });
  });

  document.getElementById("howto-close").addEventListener("click", closeHowto);
  document.getElementById("modal").addEventListener("click", function (event) {
    if (event.target === document.getElementById("modal")) closeHowto();
  });
  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape") closeHowto();
  });

  skeletons();
  refresh();
})();
</script>
</body>
</html>
)HTML";
}

}  // namespace app
