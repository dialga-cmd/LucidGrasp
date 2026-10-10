# Changelog

All notable changes are listed here in reverse chronological order. History
before 1.2.4 is on the [GitHub Releases](https://github.com/dialga-cmd/LucidGrasp/releases) page.

## [Unreleased]

- Added a new **Semantic Search** mode powered by the DINOv2-small embedding
  model. Instead of comparing pixels, it ranks the library by how related the
  image *content* is: each image is turned into a compact meaning descriptor
  and results are the closest descriptors. The model (about 24 MB) is not
  shipped — it is downloaded on first use, like the similar-search models, and
  the semantic index is built automatically the first time a semantic search
  runs. The model is fetched with `./fetch-model.sh semantic`.
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