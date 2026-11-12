# Support

LucidGrasp is maintained by one developer on a best-effort basis. There is no
service level agreement; do not expect an immediate answer.

## Where to ask

| Question | Where |
| --- | --- |
| Found a bug | **Help → Report an Issue...** in the application, or a [GitHub issue](https://github.com/dialga-cmd/LucidGrasp/issues/new) |
| Want a feature | **Help → Request a Feature...** in the application, or a [GitHub issue](https://github.com/dialga-cmd/LucidGrasp/issues/new) |
| Surface Scan or API keys | [README: Surface Scan](README.md#surface-scan) and the provider's own dashboard |
| Anything private | [adityaraj1234@duck.com](mailto:adityaraj1234@duck.com) via **Help → Contact the Developer** |

## Before you report a bug

- Check that you are on the latest version (**Updates → Check for Updates...**).
- Run the self-test and mention whether it passes: `./LucidGrasp --selftest`
  from a source build, `~/.local/bin/lucidgrasp --selftest` for a Linux
  user install, `/usr/local/bin/lucidgrasp --selftest` for a system install,
  `/Applications/LucidGrasp.app/Contents/MacOS/LucidGrasp --selftest` on macOS,
  or `"C:\Program Files\LucidGrasp\LucidGrasp.exe" --selftest` on Windows.
- Include the **LucidGrasp version** and your **operating system**.
- Give steps to reproduce, and the library size and image formats involved if
  they are relevant.

## Installer and dependency problems

- Re-run the installer with `--dry-run` (`-DryRun` on Windows) to see every
  step without changing anything.
- Linux and macOS installers install runtime dependencies from the system
  package manager. If that is not possible, use the per-distribution commands
  in [README: Runtime dependencies](README.md#runtime-dependencies), or pass
  `--no-deps` when the libraries are already present.
- On Linux, a missing `libopencv_*.so` usually means the distribution's OpenCV
  ABI differs from the Ubuntu 24.04 build. Use the Snap, OBS, or Launchpad PPA
  package, or build from source.
- `install.sh --appimage` installs the AppImage instead of the tarball on
  Linux. Both formats still use the system Qt6 and OpenCV.
- Every platform asset has a matching `.sha256` file. Verify it with
  `sha256sum <file>` on Linux or `shasum -a 256 <file>` on macOS.
- The Windows installer checks for the Microsoft Visual C++ 2015–2022 x64
  runtime and installs it when missing. If that step is blocked, install it
  manually from <https://aka.ms/vs/17/release/vc_redist.x64.exe>.
- To remove a Linux or macOS install, re-run the installer with `--uninstall`
  and the same `--prefix` or `--system` value used originally. On Windows use
  Add or Remove Programs.

## Surface Scan problems

- Open **Internet → Surface Scan — API Keys…**, validate each key, and read the
  per-provider error message in the page.
- Check the provider's quota and dashboard. Free tiers reset monthly or over a
  short window and are easy to exhaust.
- Zenserp's reverse endpoint needs a publicly reachable image URL, so local
  files are reported as unsupported.
- HasData, ScraperAPI, Scrape.do and ZenRows are registered for the
  image-to-keyword stage and are not yet used to run a search.

## How to get help in the application

- **Updates menu** — check for a new release manually, or toggle automatic
  checks.
- **Help menu** — report an issue, request a feature, contact the developer,
  and read the [terms of use](TERMS.md) and [privacy policy](PRIVACY.md).