# Security Policy

## Supported versions

Only the latest release is supported. Bug and security fixes land on `main`
and ship with the next release, so please keep the application up to date
using **Updates → Check for Updates...**.

## Reporting a vulnerability

Please do **not** open a public issue for a security vulnerability. Private
vulnerability reporting is enabled, so the preferred channel is GitHub's
private reporting form, which opens a draft security advisory visible only
to you and the maintainers:

- GitHub private vulnerability reporting (preferred):
  <https://github.com/dialga-cmd/LucidGrasp/security/advisories/new>
- Email (backup): [adityaraj1234@duck.com](mailto:adityaraj1234@duck.com)

In your report, include:

- the affected version(s),
- a description of the issue,
- steps to reproduce,
- and, if possible, a suggested fix.

Reports are handled on a best-effort basis. You will be acknowledged, and a
coordinated disclosure timeline can be arranged if needed. Submitted reports
appear as a draft security advisory in the repository's
[Security advisories](https://github.com/dialga-cmd/LucidGrasp/security/advisories)
and are not public until the advisory is published after a fix ships.

## Scope

This policy covers LucidGrasp itself: the application source in this
repository, the build configuration, and the installers and packages it
produces. Third-party dependencies (Qt6, OpenCV) are governed by their own
security processes.