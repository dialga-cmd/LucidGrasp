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

### How It Works

Each search runs three independent comparison algorithms and combines their results into a single similarity percentage.

SSIM (Structural Similarity Index) accounts for 45% of the final score. Both images are converted to grayscale and compared based on their shapes, edges, luminance patterns, and contrast. This is completely blind to color changes, which means a raw photo and its color graded edit will still score very high because the underlying structure is identical.

ORB (Oriented FAST and Rotated BRIEF) keypoint matching accounts for 35% of the final score. The algorithm detects up to 500 visually distinctive points in each image (sharp corners, high contrast edges, unique textures) and checks how many of those points exist in both images. This catches structural matches even when images have been cropped or slightly rotated.

Color histogram intersection accounts for 20% of the final score. Both images are converted to the HSV color space and a detailed histogram is built across 3000 bins tracking the exact distribution of every color in the image. The overlap between the two histograms represents the percentage of pixel colors shared between them.

Those three percentages weight the OpenCV comparison against each other. The score a result is finally shown with blends that comparison with the cheap hash ranking from stage one, at 80% to 20%, which is why an indexed file that has since changed on disk is refreshed before it is scored rather than being ranked against hashes for pixels that are no longer there.

Running those three algorithms against every indexed image would make each search slower the more you index, so searching happens in two stages. Every image is first ranked using the perceptual and difference hashes already stored in the index, which is pure in-memory arithmetic and touches no files. The highest-ranked candidates - at least 256, scaling up to a 2048 ceiling as the library grows - are then decoded and put through the full OpenCV comparison above. Because the expensive half of the work is capped, the time a search spends decoding and comparing images does not grow as the library does, while the top of the ranking stays the same as a full scan would produce. Ranking the shortlist is still a linear pass over the index, but it is arithmetic on data already in memory rather than image decoding, so it stays in the low milliseconds even for a whole-filesystem library.

### Semantic Search

Alongside Visual and Similar search, LucidGrasp offers a **Semantic Search** mode that matches by *meaning* rather than by pixels. Each image is run through DINOv2-small, a small vision transformer that turns it into a compact descriptor of what is in the picture; a search ranks the library by how close each descriptor is to the query's. Because the descriptor captures the subject rather than the exact pixels, two photos of the same kind of thing score high even when they differ in background, crop, or lighting, and the background-removal step used by Similar search is not needed at all.

The semantic model is about 24 MB and is not shipped with the application. It is fetched on first use, exactly like the similar-search models, and the semantic data is added to the index the first time a semantic search runs. You can also fetch it ahead of time with `./fetch-model.sh semantic`.

### Planned Expansion

The long term goal for this project is global discovery. The local search functionality serves as the foundation for a much larger distributed network crawler. Upcoming updates will introduce the ability to scan websites and deep web repositories for specific images. This will turn the application into a powerful asset for cybersecurity professionals and researchers who need to track the spread of sensitive media across the internet.

### Downloads

Pre built binaries are available on the Releases page. Download the archive for your operating system, extract it, and run the application.

For Linux, download `lucidgrasp-linux-x64.tar.gz`. You will still need Qt6 and OpenCV installed on your system because Linux builds link against system libraries.

```bash
sudo apt install qt6-base-dev libopencv-dev
tar -xzf lucidgrasp-linux-x64.tar.gz
cd LucidGrasp
./LucidGrasp
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

3. Fetch the prebuilt ONNX Runtime for your platform (used for background
removal and semantic search), then configure and compile the executable.

```bash
./fetch-onnxruntime.sh
mkdir build
cd build
cmake ..
make
```

4. Launch the application.

```bash
./LucidGrasp
```

If you would rather confirm the build is sound before opening a window, `--selftest` builds a synthetic image corpus, indexes it, and verifies that edited and rescaled copies rank above unrelated distractors, along with the update checker's version and payload handling. It needs no network and no display.

```bash
./LucidGrasp --selftest
```

Once running, click "Browse" under Library to select a folder containing images, then click "Index Library" to scan and index them. After indexing, select a query image and click "Search" to find all similar images above the threshold you set.

You can also point the library at `/` to index a whole filesystem. The scan follows symlinked directories (so nothing is missed) while refusing to descend into `/proc`, `/sys`, `/dev`, and `/run`, and it survives permission errors and symlink loops. Expect this to take a long while and to produce a large cache file.

The index is cached per library in `~/.local/share/LucidGrasp/indexes/`, outside the folder being scanned, so indexing a read-only location such as `/usr` works and the index is never indexed itself. Delete that file to force a rebuild.

### Building from Source (Windows)

You will need the MSVC Build Tools or Visual Studio, CMake, Qt6, and OpenCV pre built binaries for Windows.

The Qt version is the constraint worth knowing about first. Qt 6.5 LTS ships MSVC 2019 (the `v142` toolset) for Windows desktop, and no `win64_msvc2022_64` build exists for any 6.5.x release. If you try to install Qt through the Qt Online Installer and pick the MSVC 2022 64 bit component, aqt will fail to find the packages. Use the MSVC 2019 64 bit component instead, and build with the `v142` toolset.

1. Install Qt6 using the official Qt Online Installer. Select the MSVC 2019 64 bit component during installation.

2. Download the OpenCV Windows release from the official OpenCV GitHub releases page. Extract it to a known location (for example `C:\opencv`).

3. Clone the repository, then fetch the prebuilt ONNX Runtime for Windows
(run this from Git Bash, which ships with Git for Windows):

```bash
git clone https://github.com/dialga-cmd/LucidGrasp.git
cd LucidGrasp
./fetch-onnxruntime.sh
```

4. Open a Developer Command Prompt for the 2019 toolset and run:

```cmd
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\opencv\build"
nmake
```

5. Before running `LucidGrasp.exe`, use `windeployqt` to copy the required Qt DLLs into the build folder:

```cmd
windeployqt --release LucidGrasp.exe
```

Then copy the OpenCV world DLL from `C:\opencv\opencv\build\x64\vc16\bin\opencv_world*.dll` and the ONNX Runtime DLL from `third_party\onnxruntime\lib\onnxruntime.dll` into the same folder. The application is now ready to run.

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
