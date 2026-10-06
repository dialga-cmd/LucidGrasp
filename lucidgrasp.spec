# Fedora/RPM Spec File (lucidgrasp.spec)
Name:           lucidgrasp
Version:        1.2.0
Release:        1%{?dist}
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
cd %{_builddir}

%build
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make %{?-get_mac_cpu} -j$(nproc)

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
* Tue Oct 6 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.0-1
- Initial release
