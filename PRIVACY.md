# LucidGrasp Privacy Policy

Last reviewed: October 2026

LucidGrasp is designed so that your work stays on your machine. Your library,
your searches and your settings are never collected, uploaded or sold. This
document states exactly what the application does with your data so every
claim is easy to check against the source code.

The same text is available inside the application at **Help → Privacy
Policy...**.

## Data controller

The data controller for the processing described in this policy is the
project maintainer, reachable at
[adityaraj1234@duck.com](mailto:adityaraj1234@duck.com),
for all privacy requests. The project is a small, free, open-source effort; it
processes no personal data on its own behalf. The single processing activity
described below ("update checks") is limited to delivering the software to
people who choose to install it.

## Data that stays on your computer

### Indexing and searching

Indexing and matching read image files only from folders you choose. Every
similarity comparison is computed on your own machine, and no image data ever
leaves it. The application does not inspect, upload or transmit your images,
folders or search queries to anyone.

### Local cache

The search index is cached to your own disk in your system's standard
per-user data directory — for example `~/.local/share/LucidGrasp/indexes/` on
Linux — outside the folder being scanned. You can delete the cache at any
time to force a rebuild; nothing is uploaded anywhere.

### Preferences

Preferences such as whether automatic update checks are enabled and which
release version you are currently ignoring are stored in a per-user INI file
in your system's standard configuration directory. Deleting that file resets
those preferences.

## The one time data leaves your computer

### Update checks

On launch — unless you have turned automatic checks off in the **Updates**
menu — LucidGrasp makes a single HTTPS request to the GitHub API asking for
the latest release announcement of this project. The request carries only the
repository identifier and the version number already installed on your
machine. It does not include your name, your files or your library contents.

As with any website visit, GitHub receives your IP address and the ordinary
HTTP headers your browser or this application sends. GitHub Inc. (part of
Microsoft) is a company based in the United States; the request is therefore
an international transmission of technical connection data to the United
States, and GitHub's own [privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-privacy-statement)
applies to it. No other vendor receives any data from the application.

**Lawful basis (GDPR Article 6):** this request is necessary for the
legitimate interest of making sure users receive notices of security fixes and
new releases of free software they have installed (Article 6(1)(f)). The
impact on data subjects is minimal because the request is a single, anonymised
connection that reveals only routine network metadata, and it can be switched
off entirely in the Updates menu.

**Retention:** LucidGrasp stores nothing about this request beyond the local
"version being ignored" preference you set yourself. Any logs GitHub keeps are
governed by GitHub's own policies.

If a newer release exists you are shown a dialog with a link; nothing is
downloaded or installed unless you choose to do it yourself.

### Help menu links

**Report an Issue**, **Request a Feature**, **View on GitHub** and **Contact
the Developer** open whichever external service they name (GitHub or your
mail application) using your normal default apps. Any data you send through
those channels is sent by you, to those services, under their policies.

## What LucidGrasp never does

- No analytics, no telemetry and no crash reporting.
- No advertising, no cookies, no accounts, no user profiles.
- No background services that phone home.
- **No sale or sharing of personal data.** LucidGrasp does not sell, rent,
  share, or use personal information for targeted advertising. The "sale" and
  "sharing" definitions under the California Consumer Privacy Act as amended
  by the CPRA, and under comparable US state privacy laws, therefore never
  apply; there is nothing to opt out of.
- LucidGrasp does not knowingly collect personal data from children, and the
  Software is not directed to children. See "Children" below.
- No automated decision-making that produces legal or similarly significant
  effects, and no machine-learning inference: image matching uses
  deterministic, locally computed comparisons, and no user data is ever used
  to train a model.

## Children

The Software is not directed at children and is not offered as a service aimed
at them. Consistent with the US Children's Online Privacy Protection Act
(COPPA) and the child-consent age provisions of the EU General Data Protection
Regulation (member states set this age between 13 and 16), LucidGrasp does not
knowingly collect personal information from anyone under 13. If you believe a
child has sent us personal information in an issue or email, contact
[adityaraj1234@duck.com](mailto:adityaraj1234@duck.com) and it will be
deleted promptly.

## Your rights

Even though the application holds no personal data about you, these rights are
honored to the fullest extent applicable law requires. Write to
[adityaraj1234@duck.com](mailto:adityaraj1234@duck.com) to exercise any of
them. Requests are answered within the statutory deadlines (30 days under
GDPR/UK GDPR; 45 days under CCPA/CPRA).

- **Access (GDPR Art. 15) — Know (CCPA § 1798.110):** receive a copy of any
  personal data we hold about you, plus the categories and purpose of any
  processing.
- **Rectification (Art. 16) — Correct (§ 1798.106):** have inaccurate data
  corrected.
- **Erasure (Art. 17) — Delete (§ 1798.105):** have data deleted.
- **Restriction (Art. 18) / Portability (Art. 20):** restrict processing while
  a dispute is resolved, and receive data in a machine-readable format.
- **Objection (Art. 21):** object to processing based on legitimate interest.
- **No discrimination and no retaliation** for exercising a CCPA/CPRA right
  (§ 1798.125).
- **Authorized agents** may submit requests on your behalf under
  CCPA/CPRA § 1798.135(b)(1); we will verify identity as required.
- **Do Not Sell / Do Not Share:** nothing is sold or shared, so no opt-out is
  needed; this policy confirms it in writing.

**Complaints (GDPR Art. 77):** you may lodge a complaint with your local
supervisory authority. A directory of EU data protection authorities is
maintained by the European Data Protection Board.

## Changes to this policy

If the data flows of the Software change, this policy is updated in the same
release. The "Last reviewed" date above always reflects the latest state.

## Contact

Questions about this policy:
[adityaraj1234@duck.com](mailto:adityaraj1234@duck.com).