# Contributing to LucidGrasp

Thanks for considering a contribution. This guide covers how to build the
project locally, how to verify a change, and the conventions to follow.

## Building from source

Requirements: CMake 3.16+, a C++17 compiler, Qt6 (Widgets, Network and, on
Linux, DBus) and OpenCV.

```bash
sudo apt install cmake qt6-base-dev libopencv-dev g++
git clone https://github.com/dialga-cmd/LucidGrasp.git
cd LucidGrasp
mkdir build && cd build
cmake ..
make
```

The Windows build process (MSVC toolset selection, Qt and OpenCV setup,
`windeployqt`) is documented in the [README](README.md#building-from-source-windows).

## Verifying a change

Run the self-test before and after your change. It builds a synthetic image
corpus, indexes it, and verifies that edited and rescaled copies rank above
unrelated distractors, along with the update checker's version and payload
handling. It needs no network and no display:

```bash
./LucidGrasp --selftest
```

**All self-tests must pass before a pull request is opened.** If your change
is in a testable subsystem, extend the relevant section of `src/selftest.cpp`
rather than leaving behavior untested.

## Conventions

- C++17, Qt6, OpenCV. Keep new code consistent with the surrounding style:
  the existing sources use two-space indentation, Qt naming (camelCase
  methods, `m_`-free `_`-suffixed members), and lambdas for short signal
  handlers.
- User-visible strings are wrapped in `tr()`.
- Keep changes focused. A pull request should do one thing and describe it.
- No network access in the self-test; it must run headless.

## Proposing changes

1. Open an issue describing the problem or feature first, so work is not
   duplicated.
2. Work on a branch, not directly on `main`.
3. Push the branch and open a pull request that mentions the issue.
4. CI runs on every pull request and never publishes anything; it must be
   green before the change is reviewed.

## Reporting bugs

File a bug report using **Help → Report an Issue...** in the application, or
open an issue directly on GitHub. Include the LucidGrasp version, your
operating system, and steps to reproduce.