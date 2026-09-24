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

## Current Architecture

### Build System

The project uses CMake (minimum version 3.16) with C++17. Three external libraries are required:

    Qt6 Widgets for the graphical interface
    OpenCV for computer vision and pixel analysis
    pthreads for background indexing (Linux only, Windows uses native threads)

On Linux, OpenCV is installed globally on the system via the package manager (sudo apt install libopencv-dev) and CMake locates it automatically through find_package(OpenCV REQUIRED). On Windows, the OpenCV path is passed explicitly to CMake using -DOpenCV_DIR. No hardcoded paths exist in the project itself.

The CMakeLists.txt includes platform specific sections. On Windows, it appends a .rc resource file to embed the application icon into the executable and uses MSVC compatible compiler flags (/W3). On Linux, it uses GCC flags (-Wall -Wextra) and includes install targets for the binary, desktop file, and icon.

### Source Layout

    src/core/features.h and features.cpp contain the original hashing algorithms (pHash, dHash, color histogram) and the feature extraction pipeline. Every image that gets indexed has its file hash, perceptual hash, difference hash, and hue/saturation histogram computed and stored.

    src/core/cv_matcher.h and cv_matcher.cpp contain the OpenCV based matching engine. This is the primary comparison method used during search. It implements three independent algorithms: SSIM, ORB keypoint matching, and color histogram intersection.

    src/core/index.h and index.cpp manage the index data structure, serialization to disk, and the search loop that iterates over all indexed entries comparing them against a query image.

    src/ui/mainwindow.h and mainwindow.cpp implement the Qt6 graphical interface including the library browser, query image selector, threshold control, progress bar, and results grid.

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

    score = (0.45 * ssimScore) + (0.35 * orbScore) + (0.20 * colorScore)

Two of the three signals (SSIM and ORB) are color invariant, meaning 80% of the total score is immune to color grading. This design decision was made specifically to handle the real world scenario where the same photograph exists in multiple edited versions with different color treatments.

### Threshold Control

The user interface includes a spinbox labeled "Threshold (%)" that defaults to 50. During the results display phase, any image whose similarity score falls below this threshold is filtered out and not shown. The user can adjust this value before running a search to control how strict the matching should be.

### Indexing

The indexing process scans a directory recursively for all supported image formats (png, jpg, jpeg, bmp, gif, and any additional formats supported by the Qt image reader plugins installed on the system). For each image found, it extracts the feature set (file hash, pHash, dHash, color histogram) and stores the results in memory. Once complete, the index is serialized to a binary file named .image_search_index.bin in the root of the scanned directory. On subsequent launches, the application can load this cached index instantly without rescanning.

Indexing runs on a background thread and reports progress back to the UI through Qt's queued connection mechanism. The user can cancel the indexing at any time.

## Release Pipeline

The project uses GitHub Actions to automatically build and package releases for both Linux and Windows whenever a new release is published on GitHub.

### Linux Build

The Linux job runs on ubuntu-latest. It installs Qt6, OpenCV, and g++ through apt, builds the project with make, and packages the executable along with the icon, desktop file, README, and license into a tar.gz archive named lucidgrasp-linux-x64.tar.gz. Users still need Qt6 and OpenCV installed on their system to run the binary because Linux builds dynamically link against system libraries.

### Windows Build

The Windows job runs on windows-latest. It installs Qt6 using the jurplel/install-qt-action GitHub Action, downloads the official OpenCV pre built Windows binaries, sets up MSVC through ilammy/msvc-dev-cmd, and builds the project with NMake. After compilation, it runs windeployqt to automatically copy all required Qt DLLs, plugins, and platform files into the output directory, and then copies the OpenCV world DLL alongside the executable. The entire folder is compressed into a zip file named lucidgrasp-windows-x64.zip. Windows users can extract this archive anywhere and run image_search.exe immediately with zero additional setup.

## Testing Results

The indexing system has been tested and confirmed to work correctly on directories containing over 400 images. It processes all supported formats without crashing and produces accurate cached index files that can be reloaded on subsequent runs.

The matching engine has been tested with both raw and edited versions of photographs. Tests included an abandoned building photograph with a heavy teal/cyan color grade applied, and a portrait photograph with shadow crushing and color tone adjustments. In both cases, the edited version was correctly identified as a match. The portrait test returned 100% for the edited image and 50% for the raw version against a directory of 10 mixed images.

## Known Issues

There is a crash that occurs when performing a search on large indexed directories (100+ images). The indexing itself completes without any problems, but when the user selects an image and initiates a search against the full index, the application gets killed by the operating system. The search function loads every indexed image from disk and resizes it to 256x256 before running the OpenCV comparison, which significantly reduces per image memory usage compared to the original full resolution approach. Despite this, processing hundreds of images sequentially still accumulates enough memory pressure to trigger the OOM killer on systems with limited RAM. This issue will be investigated and fixed in the next development cycle. Potential solutions include processing images in smaller batches with explicit memory release between batches, or precomputing and caching the SSIM and ORB descriptors in the index file so raw images do not need to be loaded during search at all.
