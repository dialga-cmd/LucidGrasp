# LucidGrasp

LucidGrasp is a high performance image search engine built to identify and match visual media. It operates by breaking down images at the pixel level, analyzing their structural patterns, color distributions, and keypoint features to produce a precise similarity percentage between any two images. It is built with C++ and OpenCV for raw speed, and Qt6 for the graphical interface.

LucidGrasp runs on Linux and Windows.

### Current Capabilities

The software focuses on local file system analysis. You point it at a directory containing thousands of photos and it will index all of them. You can then provide a target image and the application will retrieve all visually similar files from the indexed folders. The matching engine is specifically designed to detect edited versions of images, including copies that have been color graded, contrast adjusted, shadow crushed, or had filters applied. A user configurable similarity threshold allows you to control exactly how strict the matching should be.

### How It Works

Each search runs three independent comparison algorithms against every indexed image and combines their results into a single similarity percentage.

SSIM (Structural Similarity Index) accounts for 45% of the final score. Both images are converted to grayscale and compared based on their shapes, edges, luminance patterns, and contrast. This is completely blind to color changes, which means a raw photo and its color graded edit will still score very high because the underlying structure is identical.

ORB (Oriented FAST and Rotated BRIEF) keypoint matching accounts for 35% of the final score. The algorithm detects up to 500 visually distinctive points in each image (sharp corners, high contrast edges, unique textures) and checks how many of those points exist in both images. This catches structural matches even when images have been cropped or slightly rotated.

Color histogram intersection accounts for 20% of the final score. Both images are converted to the HSV color space and a detailed histogram is built across 3000 bins tracking the exact distribution of every color in the image. The overlap between the two histograms represents the percentage of pixel colors shared between them.

### Planned Expansion

The long term goal for this project is global discovery. The local search functionality serves as the foundation for a much larger distributed network crawler. Upcoming updates will introduce the ability to scan websites and deep web repositories for specific images. This will turn the application into a powerful asset for cybersecurity professionals and researchers who need to track the spread of sensitive media across the internet.

### Downloads

Pre built binaries are available on the Releases page. Download the archive for your operating system, extract it, and run the application.

For Linux, download `lucidgrasp-linux-x64.tar.gz`. You will still need Qt6 and OpenCV installed on your system because Linux builds link against system libraries.

```bash
sudo apt install qt6-base-dev libopencv-dev
tar -xzf lucidgrasp-linux-x64.tar.gz
cd LucidGrasp
./image_search
```

For Windows, download `lucidgrasp-windows-x64.zip`. Everything is bundled inside the archive. Extract it anywhere and run `image_search.exe` directly. No additional downloads or installations are required.

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
./image_search
```

Once running, click "Browse" under Library to select a folder containing images, then click "Index Library" to scan and index them. After indexing, select a query image and click "Search" to find all similar images above the threshold you set.

### Building from Source (Windows)

You will need Visual Studio 2022 (or the MSVC Build Tools), CMake, Qt6, and OpenCV pre built binaries for Windows.

1. Install Qt6 using the official Qt Online Installer. Select the MSVC 2022 64 bit component during installation.

2. Download the OpenCV Windows release from the official OpenCV GitHub releases page. Extract it to a known location (for example `C:\opencv`).

3. Open a Developer Command Prompt for VS 2022 and run:

```cmd
git clone https://github.com/dialga-cmd/LucidGrasp.git
cd LucidGrasp
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\opencv\build"
nmake
```

4. Before running `image_search.exe`, use `windeployqt` to copy the required Qt DLLs into the build folder:

```cmd
windeployqt --release image_search.exe
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
