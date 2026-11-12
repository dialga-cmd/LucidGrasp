# LucidGrasp

<p align="center">
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/dialga-cmd/LucidGrasp/total"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases"><img alt="GitHub Releases" src="https://img.shields.io/github/v/release/dialga-cmd/LucidGrasp"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/lucidgrasp-linux-x64.tar.gz"><img alt="Available on Linux" src="https://img.shields.io/badge/Linux-available-brightgreen"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/LucidGrasp_Setup_x64.exe"><img alt="Available on Windows" src="https://img.shields.io/badge/Windows-available-brightgreen"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases"><img alt="Available on macOS" src="https://img.shields.io/badge/macOS-available-brightgreen"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/LucidGrasp-x86_64.AppImage"><img alt="AppImage" src="https://img.shields.io/badge/AppImage-available-brightgreen"></a>
  <a href="https://build.opensuse.org/package/show/home:dialga-cmd/lucidgrasp"><img alt="Published on OBS" src="https://img.shields.io/badge/OBS-published-brightgreen"></a>
  <a href="https://launchpad.net/~realdialga/+archive/ubuntu/lucidgrasp"><img alt="Published on Launchpad PPA" src="https://img.shields.io/badge/Launchpad%20PPA-published-brightgreen"></a>
  <a href="https://snapcraft.io/lucidgrasp"><img alt="Published on Snap Store" src="https://img.shields.io/badge/Snap%20Store-published-brightgreen"></a>
  <a href="https://github.com/dialga-cmd/lucidgrasp-bucket"><img alt="Published on Scoop" src="https://img.shields.io/badge/Scoop-published-brightgreen"></a>
</p>

LucidGrasp is a high performance image search engine built to identify and match visual media. It operates by breaking down images at the pixel level, analyzing their structural patterns, color distributions, and keypoint features to produce a precise similarity percentage between any two images. It is built with C++ and OpenCV for raw speed, and Qt6 for the graphical interface.

LucidGrasp runs on Linux, Windows and macOS.

### Current Capabilities

The software focuses on local file system analysis. You point it at a directory and it will index every image it can find, all the way up to an entire filesystem. There is no hardcoded limit on library size. You can then provide a target image and the application will retrieve all visually similar files from the indexed folders. The matching engine is specifically designed to detect edited versions of images, including copies that have been color graded, contrast adjusted, shadow crushed, or had filters applied. A user configurable similarity threshold allows you to control exactly how strict the matching should be.

Both indexing and searching run on background threads, so the window stays responsive on libraries of any size and both operations can be cancelled while they run.

The window takes its colours from the operating system. Every surface, accent, border and text colour is read out of the desktop palette rather than chosen by hand, so LucidGrasp follows the system light or dark scheme along with your accent colour. A square button in the top right switches between the two.

Every time the application is opened it checks GitHub for a newer release and shows a dialog when one is available, unless automatic checks are turned off. 'Check for Updates...' and 'Check for Updates Automatically' live in the Updates menu, and turning the automatic option back on runs a check right away. The checker only reads the release page — nothing is ever downloaded or installed without you asking.

The Help menu gets you in touch with the project: 'Report an Issue' and 'Request a Feature' open a pre-filled New Issue form on GitHub, 'Contact the Developer' opens your mail application, 'Privacy Policy...' explains exactly what the application does with your data, and 'About LucidGrasp' shows the installed version. The complete policy is also documented in [PRIVACY.md](PRIVACY.md).

### Surface Scan

Alongside local search, LucidGrasp includes a surface scan: given an image you select, it looks for where that image or a visually similar one appears across the open web. It is a bring-your-own-key feature. LucidGrasp ships no API key and never bundles a shared secret, so you connect an account you already control.

Choose **Internet → Surface Scan — API Keys…** and the app opens a setup page in your default browser, served from a loopback address on your own machine. There you paste a key for each provider you want, press Validate to have the app check it against the provider directly, and press Sync to commit. Keys are stored in your OS keychain when one is available and otherwise in an owner-only settings file; the page only ever sees a masked value. The setup covers the platforms that are self-serve and grant recurring free credits: SauceNAO and trace.moe for reverse lookup, SerpApi (Google Lens) and Zenserp for third-party reverse search, and HasData, ScraperAPI, Scrape.do and ZenRows for the image-to-keyword stage.

To run it, choose **Internet → Surface Search…** (`Ctrl+Shift+S`): the left panel collapses to a query-image picker, and **Search the web** runs the image across every provider you have enabled. Results open the matching page in your browser. The reverse search calls are implemented and run off the GUI thread; the image-to-keyword bridge for the text-only vendors is the next step. Zenserp's reverse endpoint needs a publicly hosted image URL, so it reports that local files are unsupported instead of failing silently.

### How It Works

Each search runs three independent comparison algorithms and combines their results into a single similarity percentage.

SSIM (Structural Similarity Index) accounts for 45% of the final score. Both images are converted to grayscale and compared based on their shapes, edges, luminance patterns, and contrast. This is completely blind to color changes, which means a raw photo and its color graded edit will still score very high because the underlying structure is identical.

ORB (Oriented FAST and Rotated BRIEF) keypoint matching accounts for 35% of the final score. The algorithm detects up to 500 visually distinctive points in each image (sharp corners, high contrast edges, unique textures) and checks how many of those points exist in both images. This catches structural matches even when images have been cropped or slightly rotated.

Color histogram intersection accounts for 20% of the final score. Both images are converted to the HSV color space and a detailed histogram is built across 3000 bins tracking the exact distribution of every color in the image. The overlap between the two histograms represents the percentage of pixel colors shared between them.

Those three percentages weight the OpenCV comparison against each other. The score a result is finally shown with blends that comparison with the cheap hash ranking from stage one, at 80% to 20%, which is why an indexed file that has since changed on disk is refreshed before it is scored rather than being ranked against hashes for pixels that are no longer there.

Running those three algorithms against every indexed image would make each search slower the more you index, so searching happens in two stages. Every image is first ranked using the perceptual and difference hashes already stored in the index, which is pure in-memory arithmetic and touches no files. The highest-ranked candidates - at least 256, scaling up to a 2048 ceiling as the library grows - are then decoded and put through the full OpenCV comparison above. Because the expensive half of the work is capped, the time a search spends decoding and comparing images does not grow as the library does, while the top of the ranking stays the same as a full scan would produce. Ranking the shortlist is still a linear pass over the index, but it is arithmetic on data already in memory rather than image decoding, so it stays in the low milliseconds even for a whole-filesystem library.

### Planned Expansion

The long term goal for this project is global discovery. Local search is the foundation; the surface scan above adds the ability to look up a selected image across the open web through provider APIs. Beyond that, a distributed crawler is planned to expand and verify results, aimed at cybersecurity professionals and researchers who need to track the spread of sensitive media. Discovery always comes from a provider API or the reverse lookup above; the crawler is a verifier, not a discovery source.

### Downloads

Pre built binaries are available on the Releases page. Use the curl installer below, a package manager, or download an archive manually.

### Install with curl

LucidGrasp ships a small installer script for each platform. It resolves the latest release, verifies the download's SHA-256 checksum, installs any missing runtime dependencies, and puts the application in the right place. Nothing in the installer changes the LucidGrasp source or the application itself.

**Linux and macOS**

```bash
curl -fsSL https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/install.sh | sh
```

**Windows** (PowerShell)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/install.ps1 | iex"
```

If you would rather inspect the script before running it:

```bash
curl -fsSL https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/install.sh -o install.sh
less install.sh
sh install.sh
```

Common installer options:

| Option | Applies to | Description |
| --- | --- | --- |
| `--version VERSION` | Linux, macOS | Install a specific release instead of the latest |
| `--prefix DIR` | Linux | Install to `DIR` instead of `~/.local` |
| `--system` | Linux, macOS | Install system-wide (`/usr/local`, or `/Applications` on macOS) |
| `--appimage` | Linux | Install the AppImage instead of the tarball |
| `--no-deps` | Linux, macOS | Do not install missing dependencies |
| `--no-verify` | Linux, macOS | Skip SHA-256 verification |
| `--keep-quarantine` | macOS | Leave the Gatekeeper quarantine flag in place |
| `--uninstall` | Linux, macOS | Remove a previous installation |
| `--dry-run` | Linux, macOS | Show what would be done without changing anything |

On Windows, `install.ps1` accepts the equivalent `-Version`, `-Destination`, `-NoDeps`, `-NoVerify`, `-Uninstall` and `-DryRun` switches.

### Package managers

LucidGrasp is also published through these channels:

| Channel | Install |
| --- | --- |
| Snap Store | `sudo snap install lucidgrasp` |
| Ubuntu PPA | `sudo add-apt-repository ppa:realdialga/lucidgrasp && sudo apt update && sudo apt install lucidgrasp` |
| Scoop (Windows) | `scoop bucket add lucidgrasp https://github.com/dialga-cmd/lucidgrasp-bucket && scoop install lucidgrasp` |
| OBS | Follow the repository instructions on the [OBS package page](https://build.opensuse.org/package/show/home:dialga-cmd/lucidgrasp) |

### Runtime dependencies

The release artifacts do not all contain the same things:

| Platform | Included in the download | Still needed on the system |
| --- | --- | --- |
| Linux | The LucidGrasp binary, desktop entry, and icon | Qt6 Core/Gui/Widgets/Network/DBus and OpenCV core/imgproc/imgcodecs/features2d. The installer adds them with your package manager unless you pass `--no-deps`. |
| macOS | Qt6 frameworks and OpenCV dylibs bundled inside `LucidGrasp.app` | Rosetta 2 on Apple Silicon (the current build is x86_64). The installer adds it automatically. |
| Windows | Qt6, OpenCV, and the application bundled inside the Inno Setup installer | Microsoft Visual C++ 2015–2022 Redistributable (x64). The installer checks for it and installs it if missing. |

To install the Linux dependencies yourself, use the command for your distribution:

```bash
# Debian / Ubuntu
sudo apt install qt6-base-dev libopencv-dev

# Fedora / Nobara
sudo dnf install qt6-qtbase opencv

# openSUSE
sudo zypper install libQt6Widgets6 libQt6Network6 libQt6DBus6 opencv

# Arch
sudo pacman -S qt6-base opencv

# Alpine
sudo apk add qt6-qtbase opencv
```

On Apple Silicon, install Rosetta 2 yourself with:

```bash
softwareupdate --install-rosetta --agree-to-license
```

On Windows, install the Microsoft Visual C++ Redistributable for Visual Studio 2015–2022 (x64) from <https://aka.ms/vs/17/release/vc_redist.x64.exe>.

If your Linux distribution ships an incompatible OpenCV ABI (the release build is linked against Ubuntu 24.04's OpenCV 4.6), use the Snap, OBS, or Launchpad PPA package instead of the tarball/AppImage, or build from source.

### Manual download

If you prefer not to run the installer, download the archive for your platform from the Releases page. Every platform asset has a matching `.sha256` file you can use to verify it.

For Linux, download `lucidgrasp-linux-x64.tar.gz`, install Qt6 and OpenCV, then extract and run it:

```bash
sudo apt install qt6-base-dev libopencv-dev
tar -xzf lucidgrasp-linux-x64.tar.gz
cd LucidGrasp
./LucidGrasp
```

For macOS, download `lucidgrasp-macos-<version>.dmg`, open it, and drag `LucidGrasp.app` to Applications. Apple Silicon also needs Rosetta 2 for the current x86_64 build:

```bash
softwareupdate --install-rosetta --agree-to-license
open /Applications/LucidGrasp.app
```

For Windows, download `LucidGrasp_Setup_x64.exe` and run it. Everything is bundled inside the installer. It installs to a standard location, adds a start menu entry and an optional desktop shortcut, and there is nothing else to download or install. To uninstall, use Add or Remove Programs in Windows Settings.

### Building from Source (Linux)

You will need CMake, Qt6, OpenCV, and a compiler that supports C++17.

1. Install the required system dependencies.

```bash
sudo apt install cmake qt6-base-dev libopencv-dev g++
```

2. Clone the repository to your local machine.

```bash
git clone https://github.com/dialga-cmd/LucidGrasp.git
cd LucidGrasp
```

3. Configure the build environment and compile the executable.

```bash
mkdir build
cd build
cmake ..
make
```

4. Launch the application.

```bash
./LucidGrasp
```

If you would rather confirm the build is sound before opening a window, `--selftest` builds a synthetic image corpus, indexes it, and verifies that edited and rescaled copies rank above unrelated distractors, along with the update checker's version and payload handling and the surface-scan key store and loopback request handling. It needs no network and no display.

```bash
./LucidGrasp --selftest
```

Once running, click "Browse" under Library to select a folder containing images, then click "Index Library" to scan and index them. After indexing, select a query image and click "Search" to find all similar images above the threshold you set.

You can also point the library at `/` to index a whole filesystem. The scan follows symlinked directories (so nothing is missed) while refusing to descend into `/proc`, `/sys`, `/dev`, and `/run`, and it survives permission errors and symlink loops. Expect this to take a long while and to produce a large cache file.

The index is cached per library in `~/.local/share/LucidGrasp/indexes/`, outside the folder being scanned, so indexing a read-only location such as `/usr` works and the index is never indexed itself. Delete that file to force a rebuild.

### Building from Source (Windows)

You will need the MSVC Build Tools or Visual Studio, CMake, Qt6, and OpenCV pre built binaries for Windows.

The Qt version is the constraint worth knowing about first. Use the Qt 6.8 LTS release, which ships the MSVC 2022 (the `v143` toolset) build for Windows desktop. Qt 6.5 only shipped MSVC 2019 builds, so it is no longer used.

1. Install Qt6 using the official Qt Online Installer. Select the MSVC 2022 64-bit component during installation.

2. Download the OpenCV 4.14.0 Windows release from the official OpenCV GitHub releases page. Extract it to a known location (for example `C:\opencv`).

3. Open a Developer Command Prompt for the 2022 toolset and run:

```cmd
git clone https://github.com/dialga-cmd/LucidGrasp.git
cd LucidGrasp
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\opencv\build"
nmake
```

4. Before running `LucidGrasp.exe`, use `windeployqt` to copy the required Qt DLLs into the build folder:

```cmd
windeployqt --release LucidGrasp.exe
```

Then copy the OpenCV world DLL from the `bin` directory under `C:\opencv\opencv\build\x64\` (for example `vc16\bin\opencv_world*.dll`) into the same folder. The application is now ready to run.

### Building from Source (macOS)

You will need the Xcode command line tools, CMake, Qt 6.8, and OpenCV 4.

```bash
xcode-select --install
brew install cmake qt@6 opencv@4
./make-icns.sh lucidgrasp.png lucidgrasp.icns
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" \
  -DOpenCV_DIR="$(brew --prefix opencv@4)/lib/cmake/opencv4" \
  -DLUCIDGRASP_ICNS="$PWD/lucidgrasp.icns"
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

The result is `build/LucidGrasp.app`; open it with `open build/LucidGrasp.app`. Run `QT_QPA_PLATFORM=offscreen ./build/LucidGrasp.app/Contents/MacOS/LucidGrasp --selftest` to verify the build without opening a window.

### System Installation (Linux Only)

To install the application globally so it appears in your application drawer with its icon, run the following from within the `build` directory:

```bash
sudo cmake --install .
sudo gtk-update-icon-cache -f -t /usr/local/share/icons/hicolor
```

To uninstall the application, run this command from the `build` directory:

```bash
sudo xargs rm < install_manifest.txt
```

The curl installer does the same job without a build: `sh install.sh --system` installs under `/usr/local`, and `sh install.sh --system --uninstall` removes exactly the files it recorded.

### Documentation

**Legal**

- [Legal Notices](LEGAL.md) — index of every legal document in the project.
- [Terms of Use](TERMS.md) — the rules for using the Software.
- [Privacy Policy](PRIVACY.md) — what LucidGrasp does and does not send anywhere.
- [Warranty Disclaimer](DISCLAIMER.md) — the "as is" guarantee and the limits of liability.
- [Acceptable Use](ACCEPTABLE_USE.md) — lawful and authorized use.
- [License](LICENSE) — the MIT License.

**Project**

- [Contributing Guide](CONTRIBUTING.md) — building, testing, and code conventions.
- [Security Policy](SECURITY.md) — how to report a vulnerability.
- [Support](SUPPORT.md) — where to get help.
- [Changelog](CHANGELOG.md) — release history.
