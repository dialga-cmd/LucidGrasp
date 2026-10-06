# SUSE/openSUSE Spec File (lucidgrasp.spec)
Name:           lucidgrasp
Version:        1.2.0
Release:        0
Summary:        High-performance image search engine
License:        MIT
URL:            https://github.com/dialga-cmd/LucidGrasp
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  qt6-base-devel
BuildRequires:  opencv-devel
BuildRequires:  gcc-c++

%description
LucidGrasp is a high-performance image search engine built to identify and match visual media.

%prep
# SCM Sync puts us in the source root.
# We explicitly set the build directory to avoid any path confusion.
cd %{_builddir}

%build
# Use -S . to explicitly define the source directory as the current directory
# and -B build to create the build directory.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

%install
# Use the install target from the build directory
cmake --install build --prefix /usr

%files
/usr/bin/LucidGrasp
/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop
/usr/share/icons/hicolor/256x256/apps/io.github.dialga_cmd.LucidGrasp.png
/usr/share/doc/lucidgrasp/LICENSE
/usr/share/doc/lucidgrasp/README.md

%changelog
* Tue Oct 6 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.0-0
- Initial OBS release
