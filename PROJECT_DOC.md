# LucidGrasp Project Documentation

This document covers the internal workings of LucidGrasp, the decisions made during development, and the current state of the software. It is written for contributors and for future reference when expanding the codebase.

## Development Process

This project was developed with the assistance of Google Antigravity (an AI coding assistant). The developer described what the software should do, identified problems during testing, proposed algorithm ideas, and made design decisions. The AI assistant wrote the implementation code, debugged build failures, restructured the matching engine across multiple iterations, set up the build system and packaging, and documented everything. Every major architectural decision in this document was made by the developer based on real test results, and the AI translated those decisions into working code.

## Project History

LucidGrasp started as a local image similarity search tool. The original implementation used perceptual hashing (pHash), difference hashing (dHash), and color histogram intersection to compare images. These algorithms work by shrinking images down, extracting frequency domain information through a Discrete Cosine Transform, and comparing the resulting binary fingerprints using Hamming distance.

### CLIP Attempt and Failure

An attempt was made to integrate CLIP (Contrastive Language Image Pretraining) through ONNX Runtime for semantic image understanding. The idea was that a neural network could understand what an image "means" rather than just what it looks like at the pixel level. The AI assistant set up the ONNX Runtime integration, downloaded the CLIP model, and wired it into the search pipeline. Testing revealed that the model consistently produced unreliable similarity scores. A photograph of an abandoned building and a portrait of a person were being scored as 86% similar, which is completely unacceptable for a tool that needs to produce trustworthy results. The developer identified this as a fundamental problem with semantic matching for this use case. The ONNX Runtime dependency and all CLIP related code were removed entirely from the codebase.

### First OpenCV Attempt

The developer proposed a new approach: break the image down at the pixel level, extract color information, shadows, and every minor detail, then compare those raw properties between two images. The AI assistant implemented this using OpenCV with ORB keypoint matching and color histogram intersection with a 40/60 weight split.

Testing revealed a critical flaw: color grading completely destroyed the similarity score. The developer tested with real photographs: a raw image of an abandoned building compared against its color graded edit (a teal/cyan tint applied to the same image). The algorithm failed to recognize them as the same image. The color histogram saw entirely different color distributions and tanked the score, even though the structural content was pixel for pixel identical. The same problem appeared with portrait photography: a raw image and its shadow crushed edit were scored at only 50% similarity.

The root cause was that the color histogram carried 60% of the total weight. Any edit that shifted colors (tinting, grading, contrast adjustment, filter application) would obliterate the histogram overlap and drag the final score down regardless of how structurally identical the images were.

### Final Solution

The AI assistant fixed this by introducing SSIM (Structural Similarity Index) as the dominant comparison signal and rebalancing all three algorithm weights. SSIM compares images in grayscale, making it completely blind to color changes. The new weight split was set to 45% SSIM, 35% ORB, and 20% color histogram. This means 80% of the final score comes from color invariant signals. The developer confirmed this solution worked correctly: edited images were now properly recognized as matches regardless of what color grading had been applied.

### Packaging and Distribution

The developer wanted LucidGrasp to feel like a real installed application, not just a compiled binary. The AI assistant generated an application icon (the LG logo with overlapping squares), created a Linux .desktop file for the application drawer, added CMake install targets, and set up a GitHub Actions workflow for automated release builds. The icon initially failed to appear in the app drawer because it was in JPEG format and the GTK icon cache requires PNG. The AI converted it and added the icon cache refresh command to the install instructions.

The developer then requested Windows support. The AI assistant rewrote the GitHub Actions workflow to include a separate Windows build job that compiles the project with MSVC, bundles all Qt6 and OpenCV DLLs using windeployqt, and packages everything into a self contained zip file. The CMakeLists.txt was updated to handle both platforms: embedding the icon via a Windows resource file on MSVC, and using platform appropriate compiler flags.

### 1.1 Whole Filesystem Scanning

Symlinked directories became traversable, which is what makes a scan of `/` viable at all, and the kernel and device pseudo-filesystems were excluded. The full account is under Indexing.

### 1.2.0 Update Checking, Native Dialogs, and OS Colours

This release is three things: a new feature, a correctness sweep through the search path, and the removal of hand-picked colours from the interface.

**The update checker is new.** `src/app/update_checker.{h,cpp}` asks GitHub for the latest published release, compares its tag against the running version, and raises a dismissible notice. It notifies only; it never downloads or installs. The three hard constraints behind its design are written up under Update Checking: GitHub's rate limit is per IP rather than per user, so the throttle is on attempts rather than successes; every failure resolves to silence; and an unreadable version tag resolves to silence too, because the only way to be wrong in that direction is to tell every user to update on every launch with no way to tell the answer is wrong.

**The shortlist cap was not actually present in 1.1.** Stage one collected every entry that passed the relaxed bound and stage two decoded and scored all of them, so the two-stage design did no work. See Two-Stage Search for why the cap is the only thing that makes it work. Alongside it: byte-identical copies are now collected separately and never compete for a shortlist slot; candidates and queries are both decoded through `loadScaled`, which shrinks on the way in and applies EXIF orientation, instead of being decoded at full resolution and then resized; and an entry whose file has changed since indexing has its features refreshed before it is scored, instead of being compared against hashes for pixels that are no longer there.

**Five further fixes**, each of which was a silent wrong answer rather than a crash: system directories are matched on resolved path rather than on leaf name, so a user folder called `dev` or `run` is no longer silently dropped and those losses never reached the error counter; `load` leaves `root_` set against an empty index on two of its failure paths, breaking the invariant that a root only ever describes a populated index; the cache format moved to version 2, carrying the file size and mtime that make the staleness check above possible, and version 1 caches are rebuilt rather than trusted; both worker threads now have exception barriers, because an exception crossing a `std::thread` calls `terminate` and OpenCV throws on malformed input, which is precisely what a whole-filesystem scan of untrusted files produces; and images were decoded at full resolution and then copied into an OpenCV matrix before being squeezed down to the 256 px the comparison actually reads.

**Every colour now comes from the operating system.** All eighteen style tokens are derived from `QPalette`; no hex value is chosen by hand. Roles the palette has no equivalent for are derived from it rather than guessed. See Appearance.

**The file pickers were not using the platform dialog after all.** `Qt::AA_DontUseNativeDialogs` was set as a global application attribute to work around a GTK3 bug, which meant the platform picker was suppressed on Windows and macOS as well — the one place the OS can legitimately own how a dialog looks. It is now scoped to Linux, which is the only platform with the bug.

**The version string had three independent copies** — `CMakeLists.txt`, `main.cpp` and `installer.iss` — which is the exact drift that makes an update checker report a phantom update forever. There is now one source.

## Current Architecture

### Build System

The project uses CMake (minimum version 3.16) with C++17. Three external libraries are required:

    Qt6 Widgets for the graphical interface
    Qt6 Network for the update check (ships in qt6-base-dev; windeployqt bundles the DLL)
    OpenCV for computer vision and pixel analysis
    pthreads for background indexing (Linux only, Windows uses native threads)

On Linux, OpenCV is installed globally on the system via the package manager (sudo apt install libopencv-dev) and CMake locates it automatically through find_package(OpenCV REQUIRED). On Windows, the OpenCV path is passed explicitly to CMake using -DOpenCV_DIR. No hardcoded paths exist in the project itself.

The CMakeLists.txt includes platform specific sections. On Windows, it appends a .rc resource file to embed the application icon into the executable and uses MSVC compatible compiler flags (/W3). On Linux, it uses GCC flags (-Wall -Wextra) and includes install targets for the binary, desktop file, and icon.

### Source Layout

    src/core/features.h and features.cpp contain the original hashing algorithms (pHash, dHash, color histogram) and the feature extraction pipeline. Every image that gets indexed has its file hash, perceptual hash, difference hash, and hue/saturation histogram computed and stored.

    src/core/cv_matcher.h and cv_matcher.cpp contain the OpenCV based matching engine. This is the primary comparison method used during search. It implements three independent algorithms: SSIM, ORB keypoint matching, and color histogram intersection.

    src/core/index.h and index.cpp manage the index data structure, serialization to disk, and the search loop that iterates over all indexed entries comparing them against a query image.

    src/ui/mainwindow.h and mainwindow.cpp implement the Qt6 graphical interface including the library browser, query image selector, threshold control, progress bar, results grid, theme derivation, menu bar, and the update notice.

    src/app/update_checker.h and update_checker.cpp hold the release check and everything it needs to be testable without a network: version comparison and payload parsing are free functions, the preferences sit behind a small class over `QSettings`, and only the class that owns the `QNetworkAccessManager` touches the network.

### How the Matching Engine Works

When a user submits a query image, the search function loads every indexed image from disk and passes both the query and the candidate to the CvMatcher::match function. Both images are first resized to a common 256x256 resolution to ensure consistent comparison regardless of the original dimensions and to reduce memory consumption. The function then produces a similarity score between 0.0 and 1.0 using three independent techniques.

#### Structural Similarity Index (45% of the final score)

Both images are converted to grayscale. SSIM then compares the two grayscale images by analyzing three components: luminance (overall brightness patterns), contrast (variation in brightness), and structure (spatial arrangement of pixels). It applies a Gaussian blur to compute local statistics across 11x11 pixel windows, then combines these statistics using a formula that produces a value between 0.0 (completely different structure) and 1.0 (identical structure).

Because this operates entirely in grayscale, it is completely blind to color grading, hue shifts, tinting, and saturation changes. A raw photograph and its color graded edit will produce a very high SSIM score because the underlying structure (edges, shapes, contrast patterns) remains identical. This single addition solved the problem of edited images not being recognized as matches.

#### Structural Feature Matching (35% of the final score)

The ORB (Oriented FAST and Rotated BRIEF) algorithm detects up to 500 keypoints in each image. These keypoints represent visually distinctive locations such as sharp corners, high contrast edges, and unique texture patterns. For each keypoint, ORB computes a binary descriptor that encodes the local pixel neighborhood around that point. The descriptors from both images are then matched using a brute force Hamming distance matcher with k nearest neighbors (k=2). A strict Lowe's Ratio Test is applied: a match is only accepted if the closest neighbor's distance is less than 75% of the second closest neighbor's distance. This eliminates ambiguous matches that would otherwise inflate the similarity score with false positives. ORB also operates on intensity gradients rather than raw color values, making it another color invariant signal.

#### Pixel Color Distribution (20% of the final score)

Both images are converted from RGB to HSV color space. A two dimensional histogram is computed for each image across 50 hue bins and 60 saturation bins, giving a total of 3000 bins. This histogram captures the exact distribution of colors in the image. The histograms are normalized so that the sum of all bins equals 1.0, representing 100% of the pixels in the image. The intersection of the two histograms is then calculated using cv::compareHist with the HISTCMP_INTERSECT method. The resulting value directly represents the percentage of pixel colors that the two images share in common.

This signal is now weighted at only 20% of the final score. It still adds value by boosting the score when two images share both the same structure and the same color palette (such as exact duplicates), but it can no longer single handedly tank the score when colors have been edited.

#### Score Combination

The final similarity score is calculated as:

    score = (0.80 * cvScore) + (0.20 * baseScore)

where cvScore is the 45/35/20 SSIM/ORB/colour blend described above and baseScore is the pHash/dHash/histogram blend from `combineScore`. Two of the three cvScore signals (SSIM and ORB) are colour invariant, meaning 80% of it is immune to colour grading. This is the same weighting that shipped in 1.0; what changed in 1.1 is *how many* images get the expensive treatment, described next.

### Two-Stage Search

In 1.0 the search loop decoded every indexed image and ran the full OpenCV comparison against each one. Cost was therefore linear in library size, and because it ran on the GUI thread the window froze for the entire duration. Measured on the development machine, a 400 image library froze the UI for 27 seconds and a 29,895 image library froze it for 11 minutes 30 seconds. Peak memory was only about 150 MB, so the out-of-memory theory recorded previously was wrong: the real failure was an unresponsive event loop, which users experience as a crash.

Search is now split into two stages.

Stage one ranks every entry in memory using only the hashes already stored in the index, via `prefilterScore`. This touches no files and costs microseconds per entry, so it scales to whole-filesystem indexes. The top `kMinShortlist` entries (256) are kept, selected with `nth_element` so a whole-filesystem index does not pay for a full sort. Byte-identical copies are always retained regardless of their prefilter score, and are kept in a separate list so they never compete for a shortlist slot.

The cap is load-bearing, not an optimisation. The relaxed stage-one bound is `0.8 + 0.2 * prefilter >= threshold`, and since a prefilter score lies in [0, 1] the left side is bounded to [0.8, 1.0]. For any threshold at or below 0.8 — which is the entire 0-80% range of the default UI slider — that test is unconditionally true, so without the cap the whole library would be decoded and scored and the two-stage design would do no work at all.

Stage two decodes and re-scores only that shortlist with the full OpenCV comparison. Candidate images are decoded one at a time and released immediately, so peak memory stays flat regardless of library size.

Two further optimisations matter at scale. `CvMatcher` was split into `prepare` and `match`: turning an image into grayscale, ORB descriptors, and a normalised histogram is the expensive half of a comparison, and previously the *query* side was recomputed from scratch for every candidate. The query is now prepared once per search. Result thumbnails in the results grid are decoded straight to 128 px with `QImageReader::setScaledSize` instead of loading full resolution images.

Measured against the 1.0 binary on identical inputs:

| Library | 1.0 search | 1.1 search |
| --- | --- | --- |
| 400 images (3000x2000) | 27.4 s, frozen | 10.0 s, responsive |
| 29,895 images | 690 s, frozen | 1.3 s, responsive |

Per-query cost is now bounded by the shortlist rather than the library, so it no longer grows as more images are indexed. Top-20 overlap against 1.0 was measured over twelve query variants (byte-identical, rescaled, and cropped) of four source images: 234 of 240 results matched, and every difference was a tie at the rank 19/20 boundary where two images scored 0.482 and 0.481. All twelve true matches ranked first under both versions.

### Threshold Control

The user interface includes a spinbox labeled "Threshold (%)" that defaults to 50. During the results display phase, any image whose similarity score falls below this threshold is filtered out and not shown. The user can adjust this value before running a search to control how strict the matching should be.

### Appearance

The window keeps one fixed layout and draws it in whichever colours the desktop reports. Every colour comes from `QPalette`; none is hand-picked, so the app follows the system's accent and surfaces. A square icon button in the top right switches between the two schemes, and the initial scheme is read from the desktop at startup. In the scheme the desktop is actually using, its palette is applied as reported. The other scheme has to be constructed, because Qt 6.4 can neither report nor hold a palette for a scheme that is not current: lightness is inverted with hue and saturation carried through, which keeps the desktop's accent recognisable instead of substituting a guess for it.

The palette is captured once at startup and never re-read from the application. `applyTheme` installs the derived theme as the application palette so that dialogs inherit it, so re-reading `QGuiApplication::palette()` would hand back the app's own output and make each toggle a derivation of the previous one — the colours walk away from the desktop's and never return.

Roles with no direct equivalent in the palette are derived from it rather than chosen: the muted text is the placeholder role when a style actually supplies one and otherwise the text colour washed as far toward the background as the body-text ratio allows; the hover and pressed states step the surface they sit on toward the text colour, which darkens in a light scheme and lightens in a dark one; and the primary button's label is chosen per state. That last one is not a nicety. Pressing a button darkens the accent, which walks it across the luminance at which a readable label has to flip between black and white, and Qt's own accent sits on that crossover — white reads 3.7:1 on it at rest, black 3.6:1 when pressed, so no single label clears 4.5:1 across all three states.

File pickers deliberately stay on the platform's own dialog wherever one exists, so they carry the desktop's real colours along with its previews, bookmarks and shell integration. The theme is pushed onto the dialog instance regardless, which costs nothing when a native dialog is used and covers the case where none is available: Qt then falls back to its own built-in picker, a top-level window that inherits neither the window's stylesheet nor its palette, and which otherwise renders as a white dialog carrying near-white text.

The GTK3 native picker rejects files, so `Qt::AA_DontUseNativeDialogs` is set on Linux to work around it. That attribute is a global one, and setting it unconditionally also suppressed the platform dialog on Windows and macOS, which defeated the point of leaving the pickers alone. It is now scoped to Linux, which is the only platform with the bug.

### Update Checking

`src/app/update_checker.{h,cpp}` asks GitHub for the latest published release and compares its tag against the running version. It notifies and opens the release page; it does not download or install anything. Self-updating is not viable on Linux, where the release is a tarball of a binary that may be sitting in `/usr/bin` owned by root, and the Windows equivalent would mean a 150 MB download and replacing a running executable under an installer that needs administrator rights.

The comparison is deliberately strict, and every failure resolves to silence. An unreadable tag on either side returns "not newer", because the only way to be wrong in that direction is to tell every user to update, on every launch, with no way to tell that the answer is wrong. Rejecting a leading zero in any version component is a real semver rule, and it is the one that matters here: a date-based tag such as `v2024.01.15` would otherwise parse as version 2024.1.15 and permanently outrank 1.1.0.

GitHub's unauthenticated API allows 60 requests per hour per IP address, and an IP is shared by everyone behind one NAT or carrier-grade NAT, so a single exhausted office budget silences every machine in it. The attempt is therefore recorded whatever its outcome, and the daily throttle is on attempts rather than successes: an unreachable or rate-limited GitHub costs one try a day instead of one per launch. A failure on the automatic schedule produces no output at all, because there is nothing the user can do about it and a daily complaint nobody can act on only teaches people to ignore the status bar. A failure on a manual check is reported, because that check was asked for.

Preferences live in `QSettings` under `updates/`: `disabled`, `ignoredVersion` and `lastCheck`. The dialog offers three buttons — open the download page, not now, and never check again — and the last of those sets `disabled` permanently. A permanent opt-out is a one-way door, so the menu bar carries a checkable "Check for Updates Automatically" that reverses it, and a "Check for Updates…" action that bypasses the throttle. The mute is per-version rather than a blanket switch for the same reason: someone who has ignored 1.2.0 should still hear about 1.3.0, which is the release most likely to be the one they want.

The version compared against the tag comes from `project(LucidGrasp VERSION ...)`, which hands it to `main.cpp` as `LUCIDGRASP_VERSION`. It used to be a second literal in `main.cpp` and a third in `installer.iss`, which is exactly the drift that turns an update checker into a permanent false alarm; the installer version is now read out of the CMake cache by the release workflow. The repository the checker polls is `LUCIDGRASP_REPO`, a CMake cache variable so a fork can point it elsewhere.

### Indexing

Indexing runs in two phases. The first walks the directory tree and collects the paths of all supported image formats (png, jpg, jpeg, bmp, gif, and whatever else the installed Qt image reader plugins support, which on a typical Linux install also covers tiff, webp, svg, ico, xpm, and others). The second extracts the feature set (file hash, pHash, dHash, colour histogram) for each file. Progress reports during the first phase carry a total of zero, which the UI renders as an indeterminate busy indicator; the second phase has a real total.

The traversal deliberately follows symlinked directories, because icon themes and shared asset directories are routinely symlinked and skipping them silently drops large parts of a library. An early version of this work refused to follow links and consequently missed 11,707 of 29,941 images in a system directory, which `find(1)` without `-L` cannot see either. Cycles are prevented by de-duplicating the resolved target of each symlinked directory, which is the only way a traversal can loop; a two-directory mutual symlink terminates immediately. Broken links are ignored, unreadable directories are skipped rather than treated as errors, and nesting is capped at 64 levels.

The kernel and device pseudo-filesystems are skipped, both by directory name and by resolved path, so a scan of `/` never descends into `/proc`, `/sys`, `/dev`, or `/run`, even through a symlink.

Once complete, the index is serialised to a binary file under the user's data directory:

    ~/.local/share/LucidGrasp/indexes/<folder>-<hash>.bin

The cache deliberately lives outside the scanned tree. In 1.0 it was written to the root of the scanned directory, which made indexing a read-only location such as `/` or `/usr` impossible: the save failed and, because `main.cpp` discarded the return value of `save()`, the failure was completely silent. The file name combines a readable label with a digest of the *resolved* root path, so two libraries with the same folder name never collide and a relative root such as `.` cannot be confused with a different directory spelled the same way. The pre-1.1 location is still read as a fallback so existing installs keep working; `legacyIndexPath` retains it.

Indexing runs on a background thread and reports progress back to the UI through Qt's queued connection mechanism. The user can cancel indexing at any time, during either phase. The finished index is handed to the UI through a `std::unique_ptr` so the entry vector is not deep-copied twice, which at six-figure entry counts would otherwise cost hundreds of megabytes of pointless allocation.

### Searching

Search also runs on a background thread, with progress reporting and a working Stop button, and the UI stays responsive throughout. While a search is in flight the inputs that would invalidate the in-memory index are disabled, since the worker reads the index directly rather than working on a copy.

### A Note on the Performance Table

The table under Two-Stage Search was written in 1.1 and attributes the shortlist cap to that release. The cap was not in fact present in the 1.1 binary, so those figures describe the intended design rather than a measurement of the shipped 1.1 build, and 1.2 has not been re-measured against 1.0 either. Treat the 1.0 column as the only verified figure until someone repeats the measurement on both binaries.

## Release Pipeline

The project uses GitHub Actions to automatically build and package releases for both Linux and Windows whenever a new release is published on GitHub.

### Linux Build

The Linux job runs on ubuntu-latest. It installs Qt6, OpenCV, and g++ through apt, builds the project with make, and packages the executable along with the icon, desktop file, README, and license into a tar.gz archive named lucidgrasp-linux-x64.tar.gz. Users still need Qt6 and OpenCV installed on their system to run the binary because Linux builds dynamically link against system libraries.

### Windows Build

The Windows job runs on windows-latest. It installs Qt6 using the jurplel/install-qt-action GitHub Action, downloads the official OpenCV pre built Windows binaries, sets up MSVC through ilammy/msvc-dev-cmd, and builds the project with NMake. After compilation, it runs windeployqt to automatically copy all required Qt DLLs, plugins, and platform files into the output directory, and then copies the OpenCV world DLL alongside the executable. The bundled folder is then compiled into a Windows installer with Inno Setup 6 from installer.iss, producing LucidGrasp_Setup_x64.exe. Windows users run the installer and get a start menu and optional desktop shortcut with zero additional setup. The `AppVersion` in installer.iss is rewritten from the CMake cache before the installer is compiled, so all three places that carry the version — CMake, the binary, and the installer's entry in Add/Remove Programs — are the same number.

The update checker added a dependency on `Qt6::Network`, which needs no change to either job: `qt6-base-dev` already ships it on the Linux side, and windeployqt copies `Qt6Network.dll` along with everything else on the Windows side.

Three things about the Windows job are non-obvious, and all three have broken it at least once. They are recorded here because each one fails in a way that points away from itself.

**The self test cannot run before the bundling step without help.** The self test step comes before the step that runs windeployqt and copies the OpenCV world DLL, so at that point the build directory contains nothing but the executable. Qt's own DLLs resolve because install-qt-action puts Qt's `bin` directory on `PATH`; OpenCV's do not, because OpenCV was installed to a prefix of its own. The Windows loader therefore cannot start the process, and the step fails with *no output at all* and a bare `Self-test failed` — which reads as a failing test rather than a process that never ran. The step now prepends OpenCV's `bin` directory to `PATH` and prints the numeric exit code, so that a loader failure reports `-1073741515` (`STATUS_DLL_NOT_FOUND`) and is distinguishable from a test that ran and failed. Linux never sees this, because OpenCV is a system library there and is always on the default search path.

**The version lives in the cache under `CMAKE_PROJECT_VERSION`, not `PROJECT_VERSION`.** `project(... VERSION ...)` sets `PROJECT_VERSION` as an ordinary variable, which CMake never writes to the cache; the cache entry is `CMAKE_PROJECT_VERSION:STATIC`. A grep for the bare name therefore finds nothing, and the installer step throws after every other step has already passed. When reading it fails, the step prints the `VERSION` entries that *are* present.

**`Get-Content -Raw` keeps the CRLF, and `.` matches `\r`.** The `AppVersion` rewrite runs against the raw file, so `-replace '(?m)^AppVersion=.*$'` does not stop at the line ending: a greedy `.*` consumes up to but not including the `\n`, which leaves the carriage return attached to the replaced text. The pattern is now `[^\r\n]*`. This one is worth remembering because it fails silently — the replacement still looks correct in isolation, and the damage only appears as a merged line further down the installer script.

A fourth, smaller one: `QGuiApplication` only forward-declares `QStyleHints`, so `QGuiApplication::styleHints()->colorScheme()` is a member call on an incomplete type unless `<QStyleHints>` is included. MSVC reports this as two `C2678` errors on the *comparison* lines, complaining that the left operand is an `int`, because `auto` had nothing to deduce from and fell back to one. The real error is `C2027` on the line above. This only appears on the Windows job, because only that job builds against Qt 6.5, and `colorScheme()` is the only call in the codebase gated to 6.5 or newer.

## Testing Results

The indexing system has been tested and confirmed to work correctly on directories containing over 400 images. It processes all supported formats without crashing and produces accurate cached index files that can be reloaded on subsequent runs.

The matching engine has been tested with both raw and edited versions of photographs. Tests included an abandoned building photograph with a heavy teal/cyan color grade applied, and a portrait photograph with shadow crushing and color tone adjustments. In both cases, the edited version was correctly identified as a match. The portrait test returned 100% for the edited image and 50% for the raw version against a directory of 10 mixed images.

`--selftest` runs eight cases with no network access at all, in CI on both platforms, so the update checker's behaviour is verified without spending anyone's rate limit: version comparison across 22 valid and malformed tag pairs, all of which have to resolve to silence when they are unreadable; parsing of one real-shaped payload and ten malformed ones, which must be refused without writing to the output; and a round trip of the opt-out, the per-version mute, and the daily throttle against a scratch settings directory. The live endpoint was exercised separately against the real API at three reported versions — current, older, and newer than any release — and the last of those produced no notice, which is the case that would otherwise have gone unnoticed.

## Known Issues

The previously documented crash on large libraries is resolved. The original note blamed memory exhaustion, but instrumentation showed that peak resident memory during both indexing and search stayed around 150 MB for a 400 image library, which is nowhere near an out-of-memory condition. The actual failure was that `startSearch` ran the entire search synchronously on the GUI thread. A 500 ms heartbeat timer confirmed the Qt event loop stopped being serviced for the whole duration of the search, so the window stopped painting, stopped responding to close, and had to be killed from outside, which is what a crash looks like to a user. The OOM theory was a plausible-sounding guess that measurement disproved.

What remains is a performance characteristic rather than a defect. The two-stage search bounds per-query work to the shortlist size, but each shortlisted candidate is still fully decoded and run through SSIM and ORB, so a query costs roughly 40 ms per shortlisted image on a large photograph. On a 400 image library of 3000x2000 photographs that works out to about 10 seconds. The obvious next step, if it is ever needed, is to cache a compact per-image descriptor in the index file so stage two does not have to reopen the image at all, which would cut indexing cost in exchange for a much larger index file.

There is a deliberate recall trade-off in the prefilter. It ranks on pHash, dHash, and the colour histogram, so a candidate that would score well purely on ORB keypoint overlap, such as a heavily cropped or rotated version of the query, can in principle be ranked out of the shortlist before the accurate comparison ever runs. The shortlist is 256 entries, which measured ample in testing, and the constant `ImageIndex::kMinShortlist` is the knob to raise it if unusual workloads show misses. Raising it costs proportionally more search time and nothing else.

A second, genuinely fatal bug was found and fixed at the same time. `ImageIndex::load` read an entry count straight out of the cache file and used it to resize a vector with no validation, so a corrupt or truncated file caused an enormous allocation and an unhandled `std::bad_alloc`. Verified by taking a valid 1,642 byte index, overwriting its count field with 0x7FFFFFF0, and loading it: the 1.0 binary aborts with `terminate called after throwing an instance of 'std::bad_alloc'` and dumps core, while 1.1 rejects the file and rebuilds. This is a plausible explanation for reports of the application failing to start, because a cache truncated by an earlier crash or a full disk persists and would abort every subsequent launch. `load` now cross-checks the declared count against the file size, since each entry needs at least a path, three hashes, and a 256 byte histogram.

Scanning a whole filesystem is now practical but not free. Indexing 364,505 files discovered on a typical Linux root filesystem takes on the order of an hour on spinning storage, and the resulting index file is a few hundred megabytes. The discovery phase reports an indeterminate progress bar because the total is not known until the walk finishes, which can itself take several minutes on a cold cache.

