# Changelog

All notable changes are listed here in reverse chronological order. History
before 1.2.4 is on the [GitHub Releases](https://github.com/dialga-cmd/LucidGrasp/releases) page.

## [Unreleased]

- Added `curl` installers: `install.sh` for Linux and macOS, and `install.ps1`
  for Windows. They resolve the latest release, verify published SHA-256
  checksums, install missing runtime dependencies, and support uninstall and
  dry-run modes. Both scripts are published as release assets.
- Release workflows now publish a `.sha256` file for every platform asset, and
  CI validates the installer scripts before a release can be built.
- The README documents curl installation, installer options, per-platform
  runtime dependencies, manual dependency commands, and the Linux OpenCV ABI
  caveat. The privacy, terms, acceptable-use, disclaimer, security and support
  pages were updated for Surface Scan and the new installers.

## [1.3.0] - 2026-11-11

- **Surface Scan is here: search the web with an image.** Pick a local image
  and LucidGrasp runs it through the reverse-image and image-search providers
  you have keys for, then lists the matching pages with thumbnails. Toggle
  **Internet → Surface Search…** (`Ctrl+Shift+S`) to collapse the left panel
  to a query-image picker plus a **Search the web** button.
- **Bring your own API keys.** **Internet → Surface Scan — API Keys…** opens a
  local setup page where keys are entered, validated, synced and removed.
  Keys never ship with the app; they are stored in the OS keychain when
  available, with an owner-only settings-file fallback and environment
  overrides.
- **Eight providers with recurring free tiers.** SauceNAO, trace.moe and
  SerpApi (Google Lens) run reverse-image search; Zenserp is supported for
  publicly reachable image URLs. HasData, ScraperAPI, Scrape.do and ZenRows
  are registered with validated keys for the image-to-keyword stage. Providers
  without recurring self-serve credits (Outscraper, Serpstack and Google Cloud
  Vision) were dropped.
- **Results you can sort through.** Each provider's batch is rendered as soon
  as it finishes, results are ranked by similarity across providers, and
  low-confidence noise is filtered out (SauceNAO below 0.50, trace.moe below
  the documented 0.90 cutoff). Every card shows the thumbnail, the source
  name and an **Open in browser** button; clicking the image opens it
  fullscreen at native resolution.
- Network work, thumbnail downloads and scaling run off the GUI thread, and a
  running scan can be stopped. Preview decoding is capped to stay under Qt's
  image-allocation limit.
- Validation reports each provider's own error detail, and key handling is
  hardened against leaks.
- The update dialog now shows the **complete release notes** instead of
  truncating them after 700 characters, and renders them as **Markdown**
  (headings, lists, links, bold) in a scrollable view.
- On first launch — and on every launch until it is accepted — a **Welcome**
  dialog asks you to read the legal documents: it describes the project,
  links the Privacy Policy and the Legal Notices right from the dialog, and
  notes that the documents are super small, so reading all of them takes
  only about 10 minutes. The dialog cannot be dismissed to enter the
  application: **Enter** stays red and disabled until the box "I have read
  all the documents here and will comply with all of them" is ticked, and
  only then turns green. The dialog's close button (**X**) exits LucidGrasp
  entirely instead of skipping the dialog, and the Welcome dialog returns
  on the next launch until it is agreed. Running
  `LucidGrasp --reset-legal-agreement` clears the stored agreement so the
  dialog is shown and must be agreed again.
- The in-app legal documents now render **full width**: paragraphs are
  re-flowed to the width of the dialog instead of keeping the source
  files' fixed line breaks, so the text runs from the left margin to the
  right margin instead of leaving a ragged gap.
- Fixed the generic fallback icon in the Linux dock: the application now
  advertises its desktop file name (`io.github.dialga_cmd.LucidGrasp`) so
  Wayland compositors pair the running window with the correct launcher,
  and the desktop entry gained `StartupWMClass=LucidGrasp` so X11 docks
  do the same via WM_CLASS. The app drawer was unaffected; the running
  window in the dock showed a default settings icon. Debian/PPA, RPM,
  AppImage and Snap builds all ship the same desktop entry and were all
  affected.

## [1.2.5] - 2026-10-08

- Update checks now run every time the application is opened instead of at
  most once a day. Turning **Check for Updates Automatically** back on runs a
  check immediately.
- The **Help** menu was split into an **Updates** menu (checking for updates)
  and a new **Help** menu with **Report an Issue**, **Request a Feature**
  (both open a pre-filled GitHub New Issue form), **Contact the Developer**
  (mail), **View on GitHub**, **Privacy Policy...**, **Legal Notices...** and
  **About LucidGrasp**.
- Added a complete legal set: Terms of Use, Privacy Policy with data-subject
  rights (GDPR/CCPA and comparable state laws), Warranty Disclaimer,
  Acceptable Use, and a Legal Notices index — available in the application
  and as `TERMS.md`, `PRIVACY.md`, `DISCLAIMER.md`, `ACCEPTABLE_USE.md`, and
  `LEGAL.md`.
- Contributing, Security, Support, and Changelog pages were added and are
  linked from the README.

## [1.2.4] - 2026-10-08

- Search progress is reported monotonically: prefiltering 0–50%, scoring
  50–100%, with a "Searching" percentage label.
- The shortlist of candidates sent through the full comparison now scales up
  to 2048 (previously fixed at 256) as the library grows.
- Trash is now saved synchronously before a file is deleted and failures are
  surfaced to the user instead of being silently ignored.
- Update dialogs gained **Ignore This Version**, and ignoring or suppressing
  a version shows the result in the status bar.
- A pre-built AppImage is published alongside the other release assets, and
  the README badge points at it directly.
- Packaging is auto-bumped and published from CI to OBS, Launchpad, the Snap
  Store and the Scoop bucket; the Windows installer build also updates the
  Scoop manifest.