%define    prefix    /usr/local
%define    version   0.6.3

Summary:   nexTView EPG decoder and browser
Name:      nxtvepg
Version:   %{version}
Release:   1
Requires:  tcl >= 8.3, tk >= 8.3
Source0:   nxtvepg-%{version}.tar.gz
Group:     Applications/Multimedia
Copyright: GPL
URL:       http://www.nefkom.net/tomzo/prj/nxtvepg/src/nxtvepg-%{version}.tar.gz
BuildRoot: /tmp/nxtvepg-build

%description
This is a decoder and browser for nexTView - an Electronic TV Programme Guide
for the analog domain (as opposed to the various digital EPGs that come with
most digital broadcasts). It allows you to decode and browse TV programme
listings for most of the major networks in Germany, Austria, France and
Switzerland.

Currently Nextview EPG is transmitted by:
- in Germany and Austria: Pro7, 3Sat, RTL-II.
- in Switzerland: EuroNews, SF1, TSR1.
- in France: Canal+, M6, TV5.
- in Turkey: TRT (as of June/2001 still a test transmission)

If you don't receive any of those, then this software unfortunately is
almost useless to you, except for a demo mode. For more details please
refer to the documentation in the "Help" menus or the UNIX manual page.

The Nextview standard was developed by the major European consumer electronics
manufacturers under the hood of the European Telecommunications Standards
Institute (http://www.etsi.org/) in 1995-1997. The author of this software
has no connections whatsoever to the ETSI - but he still hopes this software
distribution will be kindly tolerated.

%prep
%setup

%build
make ROOT="$RPM_BUILD_ROOT" prefix="%{prefix}"

%install
make ROOT="$RPM_BUILD_ROOT" install

%files
%defattr(-,root,root)
%doc README CHANGES COPYRIGHT TODO manual.html
%dir /usr/tmp/nxtvdb
/%{prefix}/bin/nxtvepg
/%{prefix}/man/man1/nxtvepg.1

%clean
make clean
