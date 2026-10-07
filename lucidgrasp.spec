# SUSE/openSUSE Spec File (lucidgrasp.spec)
Name:           lucidgrasp
Version:        1.2.3
Release:        1
Summary:        High-performance image search engine
License:        MIT
URL:            https://github.com/dialga-cmd/LucidGrasp
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
# Qt6 development package name differs between distros:
# openSUSE ships qt6-base-devel, Fedora/RHEL ship qt6-qtbase-devel.
%if 0%{?suse_version}
BuildRequires:  qt6-base-devel
%else
BuildRequires:  qt6-qtbase-devel
%endif
BuildRequires:  opencv-devel
Requires:       hicolor-icon-theme

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
# Install into the RPM build root; the prefix follows the distro default.
DESTDIR=%{buildroot} cmake --install build --prefix %{_prefix}

%files
/usr/bin/LucidGrasp
/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop
/usr/share/metainfo/io.github.dialga_cmd.LucidGrasp.metainfo.xml
/usr/share/icons/hicolor/256x256/apps/io.github.dialga_cmd.LucidGrasp.png
/usr/share/doc/lucidgrasp/LICENSE
/usr/share/doc/lucidgrasp/README.md
%dir /usr/share/doc/lucidgrasp

%changelog
* Wed Oct 7 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.3-1
- Bump to 1.2.3; install reverse-DNS desktop entry, icon, and AppStream metainfo
* Tue Oct 6 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.0-0
- Initial OBS release