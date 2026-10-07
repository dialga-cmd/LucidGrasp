# Debian/Ubuntu Packaging Script
# This script generates the control file for the .deb package.

PACKAGE_NAME="lucidgrasp"
VERSION="1.2.3"
MAINTAINER="dialga-cmd <adityaraj1234@duck.com>"
DESCRIPTION="High-performance image search engine built to identify and match visual media."

cat <<EOF > debian/control
Package: $PACKAGE_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: amd64
Maintainer: $MAINTAINER
Build-Depends: debhelper-compat (= 13), cmake, qt6-base-dev, libopencv-dev
Depends: qt6-base-dev, libopencv-dev
Description: $DESCRIPTION
EOF
