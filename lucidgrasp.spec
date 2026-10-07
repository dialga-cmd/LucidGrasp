Name:           lucidgrasp
Version:        1.2.3
Release:        1
Summary:        High-performance image search engine
License:        MIT
URL:            https://github.com/dialga-cmd/LucidGrasp
Source0:        %{url}/archive/refs/heads/main.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
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
%setup -q -n LucidGrasp-main

%build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

%install
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