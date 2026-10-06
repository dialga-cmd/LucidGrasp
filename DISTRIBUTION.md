# Distribution Plan

How to make LucidGrasp installable through every channel people actually use.
Written against the repo as of `f4283d7` / `v1.2.3`, October 2026.

Status legend: **[ready]** nothing blocks it — **[account]** needs a free
account you create first — **[money]** needs a paid developer program —
**[you]** I cannot do this part for policy reasons, you must.

---

## 0. The map

| Channel | What the user types | Gate | Status |
|---|---|---|---|
| AppImage | download, `chmod +x`, run | none | **[ready]** |
| AUR | `yay -S lucidgrasp` | none (uncurated) | **[account]** AUR account |
| winget | `winget install dialga-cmd.LucidGrasp` | PR + review | **[ready]** |
| Scoop | `scoop install lucidgrasp` | none (own bucket) | **[account]** |
| Chocolatey | `choco install lucidgrasp` | moderation + VT scan | **[account]** |
| OBS (apt/zypper) | `apt install lucidgrasp` *after adding repo* | free account | **[account]** |
| Launchpad PPA | `add-apt-repository ppa:dialga-cmd/lucidgrasp` | free account | **[account]** |
| Fedora Copr | `dnf copr enable dialga-cmd/lucidgrasp` | free account | **[account]** |
| Snap | `snap install lucidgrasp` | free account, auto review | **[account]** |
| Nixpkgs | `nix run nixpkgs#lucidgrasp` | review PR | **[ready]** |
| Flathub | `flatpak install flathub io.github.dialga_cmd.LucidGrasp` | review PR + AI policy | **[you]** |
| Homebrew | `brew install --cask lucidgrasp` | Gatekeeper notarization | **[money]** |
| Debian proper | `apt install lucidgrasp` (official) | ITP + DD sponsor | **[money]** time |
| Microsoft Store | Get from Store | $19 Dev Center | **[money]** |
| Mac App Store | App Store | $99 + sandbox rewrite | **[money]** |

**About `apt install`.** There is no third way. You either host your own APT
repo that users add once (openSUSE Build Service, Launchpad PPA, or a repo you
build yourself), or you get into Debian's official archive — real `debian/`
packaging against *system* Qt and OpenCV, an ITP bug report, and a Debian
Developer willing to sponsor the upload. OBS is the realistic answer: free,
builds `.deb` and `.rpm` from one source, hands back a GPG-signed repo.

---

## 1. The app ID

```
io.github.dialga_cmd.LucidGrasp
```

Fixed. It is baked into the metainfo filename, the desktop file name, the
Flatpak ID, and the Flathub submission — and Flathub treats an ID rename as a
full resubmission, so this is chosen once and never changed.

Why it is shaped this way (Flathub's *Requirements → App ID*):

- GitHub-hosted projects must use `io.github.` and at least **four** components.
  `io` + `github` + `dialga_cmd` + `LucidGrasp` = four.
- Each component may contain only `[A-Z][a-z][0-9]_`. A dash is allowed only in
  the **last** component. Your GitHub username `dialga-cmd` has a dash, so the
  domain portion converts it to `_` → `dialga_cmd`. The last component
  `LucidGrasp` keeps its casing.
- Repository URL is calculated from the domain (reversed, dash restored) plus
  the last component as-is: `https://github.com/dialga-cmd/LucidGrasp` ✓ —
  which is exactly the code-hosting check Flathub runs.
- The username and repository portions may not contain `.`. Neither does.

Reserved for the macOS bundle ID: currently `local.lucidgrasp.LucidGrasp` in
`Info.plist.in`. Unifying it with the Flatpak ID is optional and safe later.

---

## 2. Shared groundwork

Everything in Waves 2 and 3 depends on this. Do it first, in one PR.

### 2.1 AppStream metainfo — **missing entirely**

Required by Flathub, openSUSE OBS, GNOME Software, KDE Discover, and the
Ubuntu Software Center. `appstreamcli 1.0.2` is installed on this machine, so
it can be linted locally with no network.

Create `io.github.dialga_cmd.LucidGrasp.metainfo.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>io.github.dialga_cmd.LucidGrasp</id>
  <name>LucidGrasp</name>
  <summary>Semantic and pixel-perfect image search</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>MIT</project_license>
  <developer id="io.github.dialga_cmd">
    <name>dialga-cmd</name>
  </developer>
  <description>
    <p>LucidGrasp indexes a folder of images and lets you find them again by
    description, by a cropped region, or by exact pixels. It runs entirely
    locally — no upload, no account, no telemetry.</p>
    <p>Features:</p>
    <ul>
      <li>Search by a typed description</li>
      <li>Search by dragging a box over any part of an image</li>
      <li>Reveal a result in your file manager, or send it to the system trash</li>
      <li>Dark and light themes following the desktop preference</li>
    </ul>
  </description>
  <launchable type="desktop-id">io.github.dialga_cmd.LucidGrasp.desktop</launchable>
  <url type="homepage">https://github.com/dialga-cmd/LucidGrasp</url>
  <url type="bugtracker">https://github.com/dialga-cmd/LucidGrasp/issues</url>
  <url type="vcs-browser">https://github.com/dialga-cmd/LucidGrasp</url>
  <provides>
    <binary>LucidGrasp</binary>
  </provides>
  <screenshots>
    <screenshot type="default">
      <image>https://raw.githubusercontent.com/dialga-cmd/LucidGrasp/main/data/screenshots/main.png</image>
      <caption>Search results for a typed query</caption>
    </screenshot>
  </screenshots>
  <content_rating type="oars-1.1"/>
  <releases>
    <release version="1.2.3" date="2026-10-06">
      <url type="details">https://github.com/dialga-cmd/LucidGrasp/releases/tag/v1.2.3</url>
      <description>
        <p>Folder and trash actions from the results view, and a macOS
        release.</p>
      </description>
    </release>
  </releases>
</component>
```

Notes:

- Do **not** add `<categories>` or `<keywords>`. Flathub pulls both from the
  desktop file when a `<launchable>` exists, and defining them here would
  override it.
- `<metadata_license>CC0-1.0</metadata_license>` is the convention for
  metainfo; it is independent of the project's MIT license.
- The screenshot URL must be HTTPS and permanently reachable. Committing the
  PNG under `data/screenshots/` and referencing `raw.githubusercontent.com`
  is the no-infrastructure option.
- Release dates must be in the past and versions must be newest-first.
- Install to `share/metainfo/`.

Lint locally:

```bash
appstreamcli validate --explain io.github.dialga_cmd.LucidGrasp.metainfo.xml
```

Flathub runs the same check through `flatpak-builder-lint`, which treats
warnings as fatal — so zero warnings, not zero errors.

**Verified against Flathub's exact invocation** (`flatpak-builder-lint` → `appstreamcli
validate --no-net --format yaml`): the template above returns `Passed: yes`
(exit 0). The one pedantic notice — `cid-contains-uppercase-letter` on the
component ID — is **pedantic-only** and does not change the exit code, so the
mixed-case last component (`LucidGrasp`) is accepted.

**Flathub disclosure:** metainfo counts as material "included in the
application", so if you take this template as-is you must disclose it in the
Flathub submission. See §3.9.

### 2.2 Icons

Current state: one `lucidgrasp.png`, **1024×1024**, but `CMakeLists.txt`
installs it into `share/icons/hicolor/256x256/apps/`. That is a false size
claim — icon themes index by directory name, so a 1024px file is served as if
it were 256px. It happens to render, and it is wrong.

Generate the full set from the 1024px source:

```bash
sudo apt install imagemagick
mkdir -p data/icons
for s in 16 24 32 48 64 128 256 512; do
  convert lucidgrasp.png -resize ${s}x${s} data/icons/lucidgrasp-${s}.png
done
convert lucidgrasp.png -background none -resize 512x512 data/icons/lucidgrasp.svg   # optional, best-effort
```

Change the install rule to loop over the sizes and land each file in the
matching `hicolor/<size>x<size>/apps/` directory. Add `hicolor-icon-theme` to
the package `Depends`/`depends`.

### 2.3 Desktop file

Current `lucidgrasp.desktop` fails `desktop-file-validate` with one hint and
is missing the window-matching key.

- Rename to `io.github.dialga_cmd.LucidGrasp.desktop`. Flathub wants this done
  in upstream source rather than patched in the manifest; it is harmless
  elsewhere (the menu still shows `Name=LucidGrasp`).
- Add `StartupWMClass=LucidGrasp` so the taskbar/Dock associates the launcher
  icon with the running window. Confirm the real value first with
  `xprop WM_CLASS` against a running instance — Qt usually reports the binary
  name, but verify rather than assume.
- `Categories=Graphics;Utility;` triggers the "more than one main category"
  hint because both are main categories. Keep `Graphics;` and add an
  additional category such as `ImageTracking;` or drop the second main one —
  `desktop-file-validate` should exit clean, not just warn.
- Keep `Icon=lucidgrasp`. Flatpak's `rename-icon` will rewrite it at build
  time (§3.9).

### 2.4 Install rules

`CMakeLists.txt` already installs `bin`, `share/applications`, and
`share/icons/...` with **relative** destinations, so
`cmake --install build --prefix /usr` with `DESTDIR` already works for every
prefix-based packager. Adding `GNUInstallDirs` is good hygiene but is *not*
blocking — there are no libraries to put in a multiarch `libdir`.

Add the metainfo:

```cmake
if(UNIX AND NOT APPLE)
  install(FILES "${CMAKE_SOURCE_DIR}/io.github.dialga_cmd.LucidGrasp.metainfo.xml"
          DESTINATION share/metainfo)
endif()
```

### 2.5 The current Linux tarball does not work outside Ubuntu 24.04

Verified by inspecting the shipped binary:

```
NEEDED  libopencv_features2d.so.406
NEEDED  libopencv_imgproc.so.406
NEEDED  libopencv_core.so.406
NEEDED  libQt6Widgets.so.6   libQt6Network.so.6   libQt6DBus.so.6
Highest symbol version required: GLIBC_2.34
```

glibc is fine — 2.34 runs on Ubuntu 22.04+, Debian 12+, Fedora 36+. **OpenCV
is not.** The soname embeds the minor version: `406` means OpenCV 4.6. Ubuntu
22.04 ships 4.5.4 (`.so.405`), Fedora ships 4.9+ (`.so.409`), Arch ships
current (`.so.411`+). Qt's `libQt6*.so.6` is stable across Qt 6 and is *not*
a problem.

So `lucidgrasp-linux-x64.tar.gz` runs on Ubuntu 24.04 and Debian 12-class
systems, and nowhere else we have checked. The README currently implies
otherwise ("You will still need Qt6 and OpenCV installed on your system") —
installing OpenCV on Fedora does not help, because the soname will not match.

**This is the strongest argument for the AppImage** (§3.1): linuxdeploy bundles
the Qt and OpenCV `.so` files into the image, so the soname question
disappears. Until it exists, the README should name the supported distros
explicitly.

---

## 3. Channel by channel

### 3.1 AppImage — **[ready]**, do this first

No account, no review, largest reach-per-hour of anything on this list, and it
fixes §2.5.

One important constraint: GitHub's `ubuntu-latest` is 24.04 today and migrates
to 26.04 between **Oct 19 and Nov 19, 2026**. Building the AppImage there
would pin it to glibc 2.39+ (or 2.42+). Build inside an **`ubuntu:22.04`
container** instead — that is a Docker image, not the deprecated
`ubuntu-22.04` runner label, so it is unaffected by the runner retirement.

Job shape:

```yaml
appimage:
  runs-on: ubuntu-latest
  container: ubuntu:22.04        # glibc 2.35 baseline → runs on 22.04+/Debian 12+/Fedora 36+
  steps:
    - uses: actions/checkout@v4
    - name: Dependencies
      run: |
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y \
          cmake g++ ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev \
          libopencv-dev libgl1-mesa-dev libglu1-mesa-dev \
          file wget desktop-file-utils appstream
        # linuxdeploy-plugin-qt looks for `qmake`; Ubuntu ships only `qmake6`.
        ln -sf /usr/bin/qmake6 /usr/local/bin/qmake
    - name: Build
      run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
    - name: AppDir
      run: |
        DESTDIR="$PWD/AppDir" cmake --install build
        desktop-file-validate AppDir/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop
        appstreamcli validate --explain AppDir/usr/share/metainfo/*.metainfo.xml
    - name: Package
      env:
        APPIMAGE_EXTRACT_AND_RUN: "1"   # no FUSE inside containers
        VERSION: "1.2.3"
      run: |
        wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
        wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
        chmod +x linuxdeploy-*.AppImage
        ./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage
```

Things that will bite you:

- **FUSE.** Containers have no `/dev/fuse`. `APPIMAGE_EXTRACT_AND_RUN=1` is
  what lets linuxdeploy, the Qt plugin, and appimagetool run at all.
- **`qmake`.** Ubuntu's Qt 6 packages install `qmake6`, not `qmake`. The
  symlink above is the usual fix.
- **Wayland.** Without it the image only ships the X11 platform plugin and
  falls back to XWayland. Add
  `EXTRA_PLATFORM_PLUGINS=libqwayland-egl.so;libqwayland-generic.so` if you
  want native Wayland.
- **Metainfo must pass validation before this step**, because appimagetool
  checks it. That is a feature — it forces §2.1 to be done first.
- Set `VERSION` so appimagetool names the output
  `LucidGrasp-1.2.3-x86_64.AppImage`.

Add the `.AppImage` to the release assets alongside the existing tarball, and
link it from the README's Downloads section.

### 3.2 winget — **[ready]**

Your Inno Setup installer already satisfies what winget wants:
`InstallerType: inno` makes winget apply `/VERYSILENT /SUPPRESSMSGBOXES
/NORESTART /SP-` itself, and `installer.iss` already has `skipifsilent` on the
`[Run]` entry, so nothing auto-launches. No custom switches needed.

**sha256 of the v1.2.3 installer** (already computed):

```
98a3cf293010796697a1f3007ecd7c1f16d5346e671827264f09a1551ef457ca
```

URL is version-pinned and first-party, which is what their policy asks for:

```
https://github.com/dialga-cmd/LucidGrasp/releases/download/v1.2.3/LucidGrasp_Setup_x64.exe
```

**Prerequisite in `installer.iss`.** winget checks that the `Publisher` field
in the manifest matches the ARP (Add/Remove Programs) entry. Your `[Setup]`
section sets no `AppPublisher`, so ARP has nothing to match against. Add:

```ini
AppPublisher=dialga-cmd
AppPublisherURL=https://github.com/dialga-cmd
AppSupportURL=https://github.com/dialga-cmd/LucidGrasp/issues
AppUpdatesURL=https://github.com/dialga-cmd/LucidGrasp/releases
AppId=LucidGrasp
```

`AppId` is worth pinning explicitly. Inno derives the uninstall key from it;
locking it to the current value makes the ARP entry stable if `AppName` ever
changes. Do **not** switch to a `{{GUID}}` — that would orphan the uninstall
entries created by 1.2.2 and 1.2.3 already in the wild.

Three files under `manifests/d/dialga-cmd/LucidGrasp/1.2.3/` (first path
segment = lowercase first letter of the publisher segment):

`LucidGrasp.yaml`

```yaml
# yaml-language-server: $schema=https://aka.ms/winget-manifest.version.1.12.0.schema.json
PackageIdentifier: dialga-cmd.LucidGrasp
PackageVersion: 1.2.3
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.12.0
```

`LucidGrasp.installer.yaml`

```yaml
# yaml-language-server: $schema=https://aka.ms/winget-manifest.installer.1.12.0.schema.json
PackageIdentifier: dialga-cmd.LucidGrasp
PackageVersion: 1.2.3
InstallerType: inno
Scope: machine
InstallModes:
  - interactive
  - silent
  - silentWithProgress
UpgradeBehavior: install
Installers:
  - Architecture: x64
    InstallerUrl: https://github.com/dialga-cmd/LucidGrasp/releases/download/v1.2.3/LucidGrasp_Setup_x64.exe
    InstallerSha256: 98a3cf293010796697a1f3007ecd7c1f16d5346e671827264f09a1551ef457ca
ManifestType: installer
ManifestVersion: 1.12.0
```

`LucidGrasp.locale.en-US.yaml`

```yaml
# yaml-language-server: $schema=https://aka.ms/winget-manifest.defaultLocale.1.12.0.schema.json
PackageIdentifier: dialga-cmd.LucidGrasp
PackageVersion: 1.2.3
PackageLocale: en-US
Publisher: dialga-cmd
PublisherUrl: https://github.com/dialga-cmd
PublisherSupportUrl: https://github.com/dialga-cmd/LucidGrasp/issues
PackageName: LucidGrasp
PackageUrl: https://github.com/dialga-cmd/LucidGrasp
License: MIT
LicenseUrl: https://github.com/dialga-cmd/LucidGrasp/blob/main/LICENSE
ShortDescription: Semantic and pixel-perfect image search that runs entirely on your machine.
Tags:
  - image
  - search
  - vision
  - opencv
  - qt
ManifestType: defaultLocale
ManifestVersion: 1.12.0
```

Execution:

1. Use whatever `ManifestVersion` the current PR template names — check
   `doc/manifest/README.md` before submitting; 1.12.0 is shown here because it
   is widely supported, but newer may be required.
2. Validate on a Windows box: `winget validate --manifest <path>`.
3. Test in the Windows Sandbox with `Tools\SandboxTest.ps1` from the repo —
   it verifies install, upgrade, and uninstall without touching your real
   machine.
4. Open a PR against `microsoft/winget-pkgs`, branch `master`. CI runs first,
   then a human reviewer. Typical turnaround is days.
5. If `dialga-cmd` is rejected because of the hyphen (the format allows it,
   but validate rather than trust this sentence), fall back to
   `DialgaCmd.LucidGrasp` and set `AppPublisher=DialgaCmd` to match.

Going forward: `wingetcreate update dialga-cmd.LucidGrasp --version <v> --urls
<url> --submit` opens the bump PR for you, or use the
`vedantmgank2007/winget-releaser` GitHub Action on every release.

### 3.3 AUR — **[account]**

Source-build PKGBUILD only. A `lucidgrasp-bin` variant pointing at
`lucidgrasp-linux-x64.tar.gz` would be **broken by construction** — see §2.5,
the binary hard-requires `libopencv_*.so.406` and Arch ships a newer OpenCV.
Qt would be fine (forward compatible within Qt 6); OpenCV would not.

`PKGBUILD`:

```bash
# Maintainer: dialga-cmd <dialga-cmd@users.noreply.github.com>
pkgname=lucidgrasp
pkgver=1.2.3
pkgrel=1
pkgdesc="Semantic and pixel-perfect image search engine"
arch=('x86_64')
url="https://github.com/dialga-cmd/LucidGrasp"
license=('MIT')
depends=('qt6-base' 'qt6-tools' 'opencv' 'hicolor-icon-theme')
makedepends=('cmake' 'ninja')
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')        # replace with the real sum via updpkgsums

build() {
  cmake -B build -S "$pkgname-$pkgver" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
```

Push over SSH — AUR repos are ordinary git repos:

```bash
git clone ssh://aur@aur.archlinux.org/lucidgrasp.git
# copy PKGBUILD + .SRCINFO (generate with makepkg --printsrcinfo > .SRCINFO)
cd lucidgrasp && git add .SRCINFO PKGBUILD && git commit -m "lucidgrasp 1.2.3" && git push
```

Register the name first at aur.archlinux.org. No review for new packages;
there is a community deletion-request process if you abandon it.

Updates are just a `pkgver` bump + `updpkgsums`, since `source=` already
points at the GitHub tag. Automate it with a release-triggered workflow that
pushes over SSH using a deploy key (§3.11).

### 3.4 Scoop — **[account]**

A bucket is just a git repo of JSON manifests. Create
`github.com/dialga-cmd/scoop-bucket`, then:

```bash
scoop bucket add lucidgrasp https://github.com/dialga-cmd/scoop-bucket
scoop install lucidgrasp
```

`lucidgrasp.json`:

```json
{
  "version": "1.2.3",
  "description": "Semantic and pixel-perfect image search engine",
  "homepage": "https://github.com/dialga-cmd/LucidGrasp",
  "license": "MIT",
  "architecture": {
    "64bit": {
      "url": "https://github.com/dialga-cmd/LucidGrasp/releases/download/v1.2.3/LucidGrasp_Setup_x64.exe",
      "sha256": "98a3cf293010796697a1f3007ecd7c1f16d5346e671827264f09a1551ef457ca"
    }
  },
  "installer": {
    "script": [ "Start-Process \"$dir\\LucidGrasp_Setup_x64.exe\" -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/DIR=\"$dir\\app\"' -Wait" ]
  },
  "bin": "app\\LucidGrasp.exe",
  "checkver": "github",
  "autoupdate": {
    "architecture": {
      "64bit": {
        "url": "$version/LucidGrasp_Setup_x64.exe"
      }
    }
  }
}
```

`checkver: github` + `autoupdate` means `scoop update lucidgrasp` resolves new
releases with no manual edits. Alternatively PR the manifest into
`ScoopInstaller/Extras` for `scoop install lucidgrasp` with no bucket add —
review is gated but open.

### 3.5 Chocolatey — **[account]**

Free account at chocolatey.org gives an API key. Package layout:

```
lucidgrasp.nuspec
tools/chocolateyInstall.ps1
```

`chocolateyInstall.ps1` downloads with `Get-ChocolateyWebFile` and checksum,
then runs the Inno installer silently — same `/VERYSILENT` switches as above.
Then:

```bash
choco pack lucidgrasp.nuspec --version 1.2.3
choco push lucidgrasp.1.2.3.nupkg --source https://push.chocolatey.org/ --api-key <key>
```

Moderation is manual and takes days; the embedded installer gets a VirusTotal
scan, so expect a delay on the first submission. Their policy wants the
download URL to come from the official source — GitHub Releases qualifies
since the project homepage is the repo.

### 3.6 openSUSE Build Service (real `apt install`) — **[account]**

This is the answer to "how do I get `apt install lucidgrasp`". Free account at
build.opensuse.org, `osc` CLI for uploads.

1. Create project `home:dialga-cmd:lucidgrasp`.
2. Two source packages: a Debian source package (`.orig.tar.gz` +
   `.dsc` + `.debian.tar.xz`) and an RPM `.spec`. Both build against system
   `qt6-base-dev` / `libopencv-dev` (deb) and `qt6-qtbase-devel` / `opencv-devel`
   (rpm) — no bundling, which is what these repos expect anyway.
3. Enable repositories: `xUbuntu_24.04`, `xUbuntu_22.04`, `Debian_12`,
   `Fedora_42`, `openSUSE_Tumbleweed`.
4. Publish. Users then:

```bash
# Debian/Ubuntu
curl -fsSL https://download.opensuse.org/repositories/home:/dialga-cmd:/lucidgrasp/xUbuntu_24.04/Release.key \
  | sudo gpg --dearmor -o /etc/apt/keyrings/lucidgrasp.gpg
echo "deb [signed-by=/etc/apt/keyrings/lucidgrasp.gpg] \
  https://download.opensuse.org/repositories/home:/dialga-cmd:/lucidgrasp/xUbuntu_24.04/ ./" \
  | sudo tee /etc/apt/sources.list.d/lucidgrasp.list
sudo apt update && sudo apt install lucidgrasp

# Fedora/openSUSE
sudo zypper addrepo https://download.opensuse.org/repositories/home:/dialga-cmd:/lucidgrasp/Fedora_42/lucidgrasp.repo
```

Because metainfo is present (§2.1), the package shows up in GNOME Software and
KDE Discover on those distros. Submitting the project to
software.opensuse.org makes it findable without the manual repo step.

Caveat: OBS publishes binary repos for `.deb` but no source repo — that is
fine for this use.

### 3.7 Launchpad PPA — **[account]**

Ubuntu-only alternative to OBS. Free Launchpad account, create
`ppa:dialga-cmd/lucidgrasp`, upload a source package with `dput`. Users then
get the familiar:

```bash
sudo add-apt-repository ppa:dialga-cmd/lucidgrasp
sudo apt install lucidgrasp
```

Same `debian/` packaging effort as OBS. Pick OBS *or* this unless you
specifically want Ubuntu-only reach; OBS covers Ubuntu and everything else.

### 3.8 Fedora Copr — **[account]**

Free Fedora account + `copr-cli`. Needs an SRPM (`.spec` + sources). Then:

```bash
copr-cli build lucidgrasp-1.2.3-1.src.rpm --project lucidgrasp
dnf copr enable dialga-cmd/lucidgrasp
dnf install lucidgrasp
```

Useful property: Copr can be pointed at a GitHub repo with **SCM method** and
rebuild automatically when you push a new tag. That makes Copr the cheapest
fully-hands-off RPM channel.

### 3.9 Flatpak / Flathub — **[you]**

> **Read this before starting.** Flathub's *Requirements* page carries a
> Generative AI policy, quoted verbatim:
>
> *"Flathub manifests must not contain AI-generated or AI-assisted content."*
>
> *"AI tools or agents must not open or automate Flathub submission pull
> requests, or generate their commit messages, descriptions, review comments,
> or replies. Submitters must not request AI-agent reviews."*
>
> There is also an affirmative duty to *"disclose any AI-generated code,
> documentation, packaging, or other material they know or reasonably believe
> is included in the application or its Flathub packaging"* — evaluated at
> reviewer discretion, with no presumption of acceptance.

Concretely, for this project: **you** write the manifest and **you** open the
PR. I can explain the format, review what you wrote, and run the linters for
you. And because essentially all of this code is AI-assisted, the submission
needs that disclosure up front. Worth weighing before investing here.

Mechanics once you decide to proceed:

1. Metainfo (§2.1) must be merged into **upstream first** — Flathub explicitly
   does not want a copy in the submission PR.
2. Fork `flathub/flathub`, create branch `new-pr`, add one top-level file
   named exactly `io.github.dialga_cmd.LucidGrasp.yml`, open the PR with that
   branch as base.
3. Runtime: **`org.kde.Platform` / `org.kde.Sdk`**. The freedesktop runtime has
   no Qt 6; the KDE runtime does. Pick the latest branch hosted on Flathub
   (`flatpak remote-ls --runtime | grep org.kde.Platform`) — stale runtimes are
   rejected outright.
4. OpenCV is not in any runtime, and Flathub requires builds with network
   disabled, so it is a **module built from source** with a pinned URL +
   sha256. Keep the config minimal: `-DBUILD_LIST=core,imgproc,features2d
   -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_opencv_apps=OFF`.
5. Proposed `finish-args`:

   ```yaml
   finish-args:
     - --share=network            # update checker polls api.github.com
     - --share=ipc
     - --socket=fallback-x11
     - --socket=wayland
     - --device=dri
     - --filesystem=home          # index image folders
     - --talk-name=org.freedesktop.FileManager1
   ```

   `--filesystem=home` will draw reviewer comment: Flathub prefers portals,
   and this app's entire purpose is scanning folders the user points it at, so
   it is defensible — but if users should index external drives you need
   `--filesystem=host` or `removable-media`, and *that* needs a written
   justification. Decide before submitting; changing permissions later is a
   fresh review.

   **Verified against `flatpak-builder-lint`'s source:** `--filesystem=home`
   produces a *permission-review finding* (`finish-args-home-filesystem-access`)
   — the linter asks you to explain why the access is needed and a moderator
   may reject it; it is **not** a hard error. `--talk-name=org.freedesktop.
   FileManager1` and `--share=network` are not in the flagged list at all. The
   rest of the set contains nothing the linter flags as a hard error (no
   x11/wayland conflict, `ipc` present with `fallback-x11`, no reserved
   paths).
6. By default Flathub builds **x86_64 and aarch64**. CI only produces x86_64
   today. Either add `flathub.json` with `{"only-arches": ["x86_64"]}` or
   arrange an aarch64 build. Submitting with a broken aarch64 build fails.
7. Screenshots must be HTTPS-reachable — see §2.1.
8. Lint before opening anything:

   ```bash
   flatpak install --user flathub org.flatpak.Builder
   flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
     manifest io.github.dialga_cmd.LucidGrasp.yml
   flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
     appstream io.github.dialga_cmd.LucidGrasp.metainfo.xml
   ```

**Two functional gaps to resolve before submitting**, both discovered by
reading the code rather than guessing:

- **Trash depends on an external binary.** `src/ui/file_actions.cpp` locates
  `gio` via `QStandardPaths::findExecutable`, falling back to `trash-put`.
  Neither is guaranteed to exist inside the KDE runtime — Qt does not pull in
  GLib. Verify with `flatpak run --command=gio <runtime> gio --help`. If
  `gio` is absent, trash reports "No trash tool is available on this system"
  to every Flatpak user. Fix options: stage `gio`, or implement the
  freedesktop trash spec in-process (`~/.local/share/Trash/{files,info}`),
  which removes the external dependency everywhere, not just in the sandbox.
- **Reveal needs the D-Bus name.** The `--talk-name=org.freedesktop.FileManager1`
  above covers it; without it the reveal button silently degrades to the
  launch-fallback path.

### 3.10 Homebrew — **[money]**

Blocked as of **September 1, 2026**. From Homebrew's *Acceptable Casks*:

> *"On macOS, apps, installers and other executable artefacts that Gatekeeper
> can assess must pass Homebrew's Gatekeeper checks and must not require
> System Integrity Protection or Gatekeeper to be disabled or bypassed."*

`lucidgrasp-macos-1.2.3.dmg` is ad-hoc signed and **not notarized**, so a cask
pointing at it would be disabled. The other door is shut from the other side —
the same docs state that *"a formula whose primary output is a native macOS
`.app` bundle is not eligible"* for homebrew/core. So:

- **Official cask** — requires the **Apple Developer Program ($99/yr)**,
  Developer ID signing, and notarization (`xcrun notarytool submit
  --wait`). Once the DMG is notarized, a cask is straightforward: `url` +
  `sha256` + `app "LucidGrasp.app"`. This is the only path to
  `brew install --cask lucidgrasp`.
- **Personal tap, cask** — `brew tap dialga-cmd/tap`, no review, works today.
  Homebrew still applies the quarantine attribute, so users hit "cannot be
  opened because the developer cannot be verified" and must right-click →
  Open. Friction, not failure.
- **Personal tap, formula** — the option worth actually considering. A formula
  builds from source against `qt@6` and `opencv` from Homebrew (both ship
  bottles, so only LucidGrasp compiles). Locally-built files carry no
  quarantine attribute, so **Gatekeeper never triggers** and no $99 is needed.
  Install the `.app` to `libexec/LucidGrasp.app` and add a `bin/lucidgrasp`
  wrapper. Note the trade-off: it stops tracking upstream Qt/OpenCV ABI only
  as far as Homebrew does, and builds break when brew bumps things.

  ```ruby
  class LucidGrasp < Formula
    desc "Semantic and pixel-perfect image search"
    homepage "https://github.com/dialga-cmd/LucidGrasp"
    url "https://github.com/dialga-cmd/LucidGrasp/archive/refs/tags/v1.2.3.tar.gz"
    sha256 "<sum>"
    license "MIT"
    head "https://github.com/dialga-cmd/LucidGrasp.git", branch: "main"

    depends_on "cmake" => :build
    depends_on "opencv"
    depends_on "qt@6"

    def install
      system "cmake", "-S", ".", "-B", "build", *std_cmake_args
      system "cmake", "--build", "build"
      system "cmake", "--install", "build"
    end

    test do
      system "#{bin}/LucidGrasp", "--selftest"
    end
  end
  ```

  Put it in `github.com/dialga-cmd/homebrew-tap` under `Formula/`, then
  `brew install dialga-cmd/tap/lucidgrasp`.

### 3.11 Keeping them updated

The version is already single-sourced from `project(LucidGrasp VERSION ...)`
and both workflows read it from `CMAKE_PROJECT_VERSION`. Extend that to a
`release-packaging.yml` triggered by `release: published`:

| Channel | Automation |
|---|---|
| winget | `wingetcreate update ... --submit`, or `vedantmgank2007/winget-releaser` |
| AUR | workflow pushes bumped `PKGBUILD`/`.SRCINFO` over SSH deploy key |
| Scoop | `checkver: github` resolves it, no CI needed |
| Copr | SCM method watching the GitHub repo — fully automatic |
| OBS | bump `Version:` in the spec and `osc commit`, or `obs_scm` service |
| Snap | `snapcore/action-build` + `snapcore/action-publish` |
| AppImage | job in the existing `release.yml` |
| Chocolatey | `choco pack` + `choco push` with a stored API key |
| Flathub | you bump `tag:`/`commit:` in the flathub PR (see §3.9) |
| Homebrew tap | bump `url`/`sha256` in the formula |

Do not enable all of these at once. Every channel you switch on is a channel
that silently goes stale when the automation breaks.

---

## 4. Recommended order

**Wave 1 — no gates, highest return.** AppImage first because it is the only
item that also fixes §2.5; then winget; then AUR. All three can be finished
without spending money or waiting on anyone.

**Groundwork first, though.** §2.1–2.4 is one PR that Waves 2 and 3 both
depend on, and it is also what makes the AppImage's appimagetool step pass.
Do it before any channel work.

**Wave 2 — free accounts.** OBS (real `apt install`), Snap, Copr. Create the
accounts, then build.

**Wave 3 — money or your own hands.** Flathub (you author it, see §3.9),
Homebrew ($99 Apple Developer for the cask, or the source formula in a
personal tap today), Debian proper (only worth pursuing if you want to find a
sponsor — budget months).

**Decide deliberately, and later:** Microsoft Store ($19 + MSIX packaging),
Mac App Store ($99 + sandbox entitlements that fight a filesystem-scanning
app), Nixpkgs (solid PR, but review queues run weeks to months).

---

## 5. Verified facts this plan rests on

| Fact | Value |
|---|---|
| Tag / commit | `v1.2.3` → `f4283d7` |
| Windows installer sha256 | `98a3cf293010796697a1f3007ecd7c1f16d5346e671827264f09a1551ef457ca` |
| Windows installer size | 60,806,523 B |
| Linux tarball size | 794,357 B |
| macOS DMG size | 139,061,318 B |
| Binary requires (OpenCV) | `libopencv_{core,imgproc,features2d}.so.406` |
| Binary requires (glibc) | max `GLIBC_2.34` |
| Icon source | 1024×1024 PNG, installed into `hicolor/256x256/` (wrong) |
| `appstreamcli` | 1.0.2, installed |
| `flatpak-builder` | not installed (`org.flatpak.Builder` from Flathub) |
| Qt in Ubuntu 24.04 apt | 6.4.2 — the `QT_VERSION_CHECK(6,5,0)` guard in `mainwindow.cpp` exists precisely for this |
| `desktop-file-validate` | one hint: two main categories |
| Metainfo | none exists |
| `AppPublisher` in `installer.iss` | not set (winget needs it) |
| Installer version source | workflow rewrites `AppVersion` from `CMAKE_PROJECT_VERSION` |
| DMG contents | `LucidGrasp.app` at image root + `Applications` symlink |
| Homebrew cask deadline | Gatekeeper enforcement active since 2026-09-01 |
| `ubuntu-latest` migration | 24.04 → 26.04, Oct 19 – Nov 19 2026 |
| `ubuntu-22.04` runner | deprecated 2026-09-17, retired 2027-04-17 (container images unaffected) |
