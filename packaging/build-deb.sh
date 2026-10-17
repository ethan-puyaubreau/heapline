#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Repackage the files of a release archive, already built and tested, as a
# Debian package for the architecture of the machine.
#
# usage: build-deb.sh ARCHIVE_DIR VERSION
#
# Writes heapline_VERSION-1_ARCH.deb in the current directory. Runs where
# dpkg-deb does, with GNU coreutils (install -D, date -R).

set -eu

if [ $# -ne 2 ]; then
  echo "usage: $0 ARCHIVE_DIR VERSION" >&2
  exit 2
fi
archive=$1
version=$2
here=$(cd "$(dirname "$0")" && pwd)

arch=$(dpkg --print-architecture)
case $arch in
  amd64) triplet=x86_64-linux-gnu ;;
  arm64) triplet=aarch64-linux-gnu ;;
  *)
    echo "$0: unsupported architecture $arch" >&2
    exit 1
    ;;
esac

name=heapline_${version}-1_${arch}
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
# mktemp makes it 0700, and the root of the package becomes / once installed.
chmod 0755 "$root"

install -D -m 0644 "$archive/lib/libheapline.so" "$root/usr/lib/$triplet/libheapline.so"
install -D -m 0755 "$archive/bin/heapline-report" "$root/usr/bin/heapline-report"
install -D -m 0644 "$archive/include/heapline.h" "$root/usr/include/heapline.h"
install -D -m 0644 "$archive/share/man/man1/heapline-report.1" "$root/usr/share/man/man1/heapline-report.1"
gzip -9n "$root/usr/share/man/man1/heapline-report.1"
install -D -m 0644 "$archive/README.md" "$root/usr/share/doc/heapline/README.md"
install -D -m 0644 "$here/debian-copyright" "$root/usr/share/doc/heapline/copyright"
install -D -m 0644 "$here/debian-lintian-overrides" "$root/usr/share/lintian/overrides/heapline"

# Debian packages carry a changelog of the packaging itself.
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  date=$(date -R -u -d "@$SOURCE_DATE_EPOCH")
else
  date=$(date -R -u)
fi
cat > "$root/usr/share/doc/heapline/changelog.Debian" <<EOF
heapline (${version}-1) unstable; urgency=medium

  * Release ${version}. See CHANGELOG.md in the source tree.

 -- Ethan Puyaubreau <ethan.puyaubreau@gmail.com>  ${date}
EOF
gzip -9n "$root/usr/share/doc/heapline/changelog.Debian"

mkdir "$root/DEBIAN"
size=$(du -sk "$root/usr" | cut -f1)
sed -e "s/@VERSION@/$version/" -e "s/@ARCH@/$arch/" -e "s/@SIZE@/$size/" \
  "$here/debian-control.in" > "$root/DEBIAN/control"
# Refresh the cache of the dynamic linker, as packages of shared libraries do.
echo "activate-noawait ldconfig" > "$root/DEBIAN/triggers"

# xz rather than the zstd of Ubuntu's dpkg, which dpkg before 1.21.18 cannot
# read (Debian 11 and older).
dpkg-deb --root-owner-group -Zxz --build "$root" "$name.deb"
