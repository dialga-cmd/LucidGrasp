# LucidGrasp

<p align="center">
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases"><img alt="Release" src="https://img.shields.io/github/v/release/dialga-cmd/LucidGrasp"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/dialga-cmd/LucidGrasp/total"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/blob/main/LICENSE"><img alt="License" src="https://img.shields.io/github/license/dialga-cmd/LucidGrasp"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/actions/workflows/release.yml"><img alt="Linux and Windows build" src="https://img.shields.io/github/actions/workflow/status/dialga-cmd/LucidGrasp/release.yml?event=release"></a>
  <a href="https://github.com/dialga-cmd/LucidGrasp/actions/workflows/release-macos.yml"><img alt="macOS build" src="https://img.shields.io/github/actions/workflow/status/dialga-cmd/LucidGrasp/release-macos.yml?event=release"></a>
  <a href="https://build.opensuse.org/package/show/home:dialga-cmd/lucidgrasp"><img alt="OBS Tumbleweed" src="https://img.shields.io/obs/home:dialga-cmd/lucidgrasp/openSUSE_Tumbleweed/x86_64?instance=https%3A%2F%2Fapi.opensuse.org&label=OBS%20Tumbleweed"></a>
  <a href="https://build.opensuse.org/package/show/home:dialga-cmd/lucidgrasp"><img alt="OBS Leap 15.6" src="https://img.shields.io/obs/home:dialga-cmd/lucidgrasp/openSUSE_Leap_15.6/x86_64?instance=https%3A%2F%2Fapi.opensuse.org&label=OBS%20Leap%2015.6"></a>
  <a href="https://build.opensuse.org/package/show/home:dialga-cmd/lucidgrasp"><img alt="OBS Leap 15.5" src="https://img.shields.io/obs/home:dialga-cmd/lucidgrasp/openSUSE_Leap_15.5/x86_64?instance=https%3A%2F%2Fapi.opensuse.org&label=OBS%20Leap%2015.5"></a>
  <a href="https://build.opensuse.org/package/show/home:dialga-cmd/lucidgrasp"><img alt="OBS build result" src="https://img.shields.io/obs/home:dialga-cmd/lucidgrasp/openSUSE_Tumbleweed/x86_64?instance=https%3A%2F%2Fapi.opensuse.org&label=OBS%20build"></a>
</p>

LucidGrasp is a high performance image search engine built to identify and match visual media. It operates by breaking down images at the pixel level, analyzing their structural patterns, color distributions, and keypoint features to produce a precise similarity percentage between any two images. It is built with C++ and OpenCV for raw speed, and Qt6 for the graphical interface.

LucidGrasp runs on Linux and Windows.

### Current Capabilities

The software focuses on local file system analysis. You point it at a directory and it will index every image it can find, all the way up to an entire filesystem. There is no hardcoded limit on library size. You can then provide a target image and the application will retrieve all visually similar files from the indexed folders. The matching engine is specifically designed to detect edited versions of images, including copies that have been color graded, contrast adjusted, shadow crushed, or had filters applied. A user configurable similarity threshold allows you to control exactly how strict the matching should be.

Both indexing and searching run on background threads, so the window stays responsive on libraries of any size and both operations can be cancelled while they run.

The window takes its colours from the operating system. Every surface, accent, border and text colour is read out of the desktop palette rather than chosen by hand, so LucidGrasp follows the system light or dark scheme along with your accent colour. A square button in the top right switches between the two.

On launch the application checks GitHub for a newer release and tells you if there is one. It does this at most once a day, and never silently: you can turn it off permanently, or ask for it on demand, from the Help menu. It only reads the release page — nothing is ever downloaded or installed without you asking.

### How It Works

Each search runs three independent comparison algorithms and combines their results into a single similarity percentage.

SSIM (Structural Similarity Index) accounts for 45% of the final score. Both images are converted to grayscale and compared based on their shapes, edges, luminance patterns, and contrast. This is completely blind to color changes, which means a raw photo and its color graded edit will still score very high because the underlying structure is identical.

ORB (Oriented FAST and Rotated BRIEF) keypoint matching accounts for 35% of the final score. The algorithm detects up to 500 visually distinctive points in each image (sharp corners, high contrast edges, unique textures) and checks how many of those points exist in both images. This catches structural matches even when images have been cropped or slightly rotated.

Color histogram intersection accounts for 20% of the final score. Both images are converted to the HSV color space and a detailed histogram is built across 3000 bins tracking the exact distribution of every color in the image. The overlap between the two histograms represents the percentage of pixel colors shared between them.

Those three percentages weight the OpenCV comparison against each other. The score a result is finally shown with blends that comparison with the cheap hash ranking from stage one, at 80% to 20%, which is why an indexed file that has since changed on disk is refreshed before it is scored rather than being ranked against hashes for pixels that are no longer there.

Running those three algorithms against every indexed image would make each search slower the more you index, so searching happens in two stages. Every image is first ranked using the perceptual and difference hashes already stored in the index, which is pure in-memory arithmetic and touches no files. Only the top 256 candidates are then decoded and put through the full OpenCV comparison above. Because the expensive half of the work is capped, the time a search spends decoding and comparing images does not grow as the library does, while the top of the ranking stays the same as a full scan would produce. Ranking the shortlist is still a linear pass over the index, but it is arithmetic on data already in memory rather than image decoding, so it stays in the low milliseconds even for a whole-filesystem library.

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

3. Open a Developer Command Prompt for the 2019 toolset and run:

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

Then copy the OpenCV world DLL from `C:\opencv\opencv\build\x64\vc16\bin\opencv_world*.dll` into the same folder. The application is now ready to run.

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
