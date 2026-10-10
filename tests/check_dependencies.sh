#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Check that an ELF file depends on the C library only, and optionally on no
# glibc symbol version above MAX_GLIBC.
#
# usage: check_dependencies.sh FILE [MAX_GLIBC]
#
# Exits 77 when readelf or objdump is missing.

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "usage: $0 FILE [MAX_GLIBC]" >&2
  exit 2
fi
file=$1
max=$2

command -v readelf >/dev/null 2>&1 || exit 77
command -v objdump >/dev/null 2>&1 || exit 77

case $max in
  *[!0-9.]* | .* | *. | *..*)
    echo "usage: $0 FILE [MAX_GLIBC]: MAX_GLIBC is digits and dots, such as 2.17" >&2
    exit 2
    ;;
esac

needed=$(readelf -d "$file") || {
  echo "FAIL: cannot read $file" >&2
  exit 1
}
undefined=
if [ -n "$max" ]; then
  undefined=$(objdump -T "$file") || {
    echo "FAIL: cannot read $file" >&2
    exit 1
  }
fi

status=0
for lib in $(printf '%s\n' "$needed" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
  case $lib in
    libc.so.6 | libm.so.6 | libdl.so.2 | libpthread.so.0 | librt.so.1) ;;
    ld-linux*) ;;
    *)
      echo "FAIL: $file needs $lib" >&2
      status=1
      ;;
  esac
done

if [ -n "$max" ]; then
  highest=$(printf '%s\n' "$undefined" | grep '\*UND\*' | grep -o 'GLIBC_[0-9.]*' |
            sed 's/^GLIBC_//' | sort -Vu | tail -n 1)
  if [ -n "$highest" ]; then
    top=$(printf '%s\n%s\n' "$highest" "$max" | sort -V | tail -n 1)
    if [ "$top" != "$max" ]; then
      echo "FAIL: $file needs GLIBC_$highest, above GLIBC_$max" >&2
      status=1
    fi
  fi
fi

exit $status
