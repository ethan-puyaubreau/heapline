# SPDX-License-Identifier: Apache-2.0
#
# Repackages the files of a release archive, already built and tested, as an
# RPM. Built on Rocky Linux 8 by the release workflow:
#
#   rpmbuild -bb packaging/heapline.spec \
#     --define "_topdir $PWD/rpmbuild" \
#     --define "heapline_version 0.4.1" \
#     --define "heapline_archive $PWD/heapline-v0.4.1-linux-x86_64"

# The package holds the files of the archive unchanged: no debug information
# to extract, no build-id links, and none of the stripping and compression of
# the brp scripts, which would rewrite the binaries checked in the archive.
%global debug_package %{nil}
%global _build_id_links none
%global __os_install_post %{nil}

Name:           heapline
Version:        %{heapline_version}
Release:        1%{?dist}
Summary:        Heap usage of a Linux program, without recompiling it
License:        Apache-2.0
URL:            https://github.com/ethan-puyaubreau/heapline

%description
heapline is a small shared library loaded with LD_PRELOAD. It intercepts the
glibc allocation functions, counts allocations and releases, and tracks the
bytes in use and their peak. A summary is printed to stderr at exit, and a CSV
record can be written for each process, MPI rank included. heapline-report
merges the records of a job.

%prep
install -m 0644 %{heapline_archive}/README.md %{heapline_archive}/LICENSE .

%install
install -D -m 0755 %{heapline_archive}/lib/libheapline.so %{buildroot}%{_libdir}/libheapline.so
install -D -m 0755 %{heapline_archive}/bin/heapline-report %{buildroot}%{_bindir}/heapline-report
install -D -m 0644 %{heapline_archive}/include/heapline.h %{buildroot}%{_includedir}/heapline.h
install -D -m 0644 %{heapline_archive}/share/man/man1/heapline-report.1 %{buildroot}%{_mandir}/man1/heapline-report.1

%ldconfig_scriptlets

%files
%license LICENSE
%doc README.md
%{_libdir}/libheapline.so
%{_bindir}/heapline-report
%{_includedir}/heapline.h
%{_mandir}/man1/heapline-report.1*
