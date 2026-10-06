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
# SCM Sync already puts us in the source root.
# We just ensure we are there.
cd %{_builddir}

%build
# We create the build directory in the root
mkdir -p build
cd build
# We use the absolute path to the source root to avoid any ".." confusion
cmake %{_builddir} -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

%install
cd build
make install DESTDIR=%{buildroot}

%files
/usr/bin/LucidGrasp
/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop
/usr/share/icons/hicolor/256x256/apps/io.github.dialga_cmd.LucidGrasp.png
/usr/share/doc/lucidgrasp/LICENSE
/usr/share/doc/lucidgrasp/README.md

%changelog
* Tue Oct 6 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.0-0
- Initial OBS release
