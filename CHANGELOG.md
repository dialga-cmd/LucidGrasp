# Changelog

All notable changes are listed here in reverse chronological order. History
before 1.2.4 is on the [GitHub Releases](https://github.com/dialga-cmd/LucidGrasp/releases) page.

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