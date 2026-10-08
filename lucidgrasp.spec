Name:           lucidgrasp
Version:        1.2.4
Release:        1
Summary:        High-performance image search engine
License:        MIT
URL:            https://github.com/dialga-cmd/LucidGrasp
Source0:        LucidGrasp-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
%if 0%{?suse_version}
BuildRequires:  qt6-base-devel
%if 0%{?sle_version} >= 150600
BuildRequires:  gcc13-c++
%define lucidgrasp_cxx g++-13
%else
%if 0%{?sle_version} >= 150400
BuildRequires:  gcc10-c++
%define lucidgrasp_cxx g++-10
%else
%define lucidgrasp_cxx g++
%endif
%endif
%else
BuildRequires:  qt6-qtbase-devel
%define lucidgrasp_cxx g++
%endif
BuildRequires:  opencv-devel
Requires:       hicolor-icon-theme

%description
LucidGrasp is a high-performance image search engine built to identify and match visual media.

%prep
%setup -q -n LucidGrasp-%{version}

%build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=%{lucidgrasp_cxx}
cmake --build build -j$(nproc)

%install
DESTDIR=%{buildroot} cmake --install build --prefix %{_prefix}

%files
/usr/bin/LucidGrasp
/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop
/usr/share/metainfo/io.github.dialga_cmd.LucidGrasp.metainfo.xml
/usr/share/icons/hicolor/256x256/apps/io.github.dialga_cmd.LucidGrasp.png
%dir /usr/share/icons/hicolor
%dir /usr/share/icons/hicolor/256x256
%dir /usr/share/icons/hicolor/256x256/apps
/usr/share/doc/lucidgrasp/LICENSE
/usr/share/doc/lucidgrasp/README.md
/usr/share/doc/lucidgrasp/PRIVACY.md
/usr/share/doc/lucidgrasp/TERMS.md
/usr/share/doc/lucidgrasp/DISCLAIMER.md
/usr/share/doc/lucidgrasp/ACCEPTABLE_USE.md
/usr/share/doc/lucidgrasp/LEGAL.md
/usr/share/doc/lucidgrasp/CHANGELOG.md
%dir /usr/share/doc/lucidgrasp

%changelog
* Wed Oct 7 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.3-1
- Bump to 1.2.3; install reverse-DNS desktop entry, icon, and AppStream metainfo
* Tue Oct 6 2026 dialga-cmd <adityaraj1234@duck.com> - 1.2.0-0
- Initial OBS release