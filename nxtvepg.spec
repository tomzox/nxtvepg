%define    prefix    /usr/local
%define    version   2.7.0

Summary:   nexTView EPG decoder and browser
Name:      nxtvepg
Version:   %{version}
Release:   0
Requires:  tcl >= 8.3, tk >= 8.3
Source0:   nxtvepg-%{version}.tar.gz
Group:     Applications/Multimedia
License:   GPL
URL:       http://prdownloads.sourceforge.net/nxtvepg/nxtvepg-%{version}.tar.gz
BuildRoot: /tmp/nxtvepg-build

%description
This is a decoder and browser for nexTView - an Electronic TV Programme Guide
for the analog domain (as opposed to the various digital EPGs that come with
most digital broadcasts). It allows you to decode and browse TV programme
listings for most of the major networks in Germany, Austria, France, Belgium
and Switzerland.

Currently Nextview EPG is transmitted by:
- in Germany and Austria: Kabel1, RTL-II (coverage: apx. 32 networks)
- in Switzerland: SF1, TSR1, TSI1, EuroNews (coverage: apx. 37 networks)
- in France: Canal+, M6, TV5, EuroNews (coverage: 8 networks)
- in Belgium: VT4 (coverage: 32 networks)
- in Turkey: TRT (coverage: apx. 17 networks)

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
%attr(777,root,root) /usr/tmp/nxtvdb
/%{prefix}/bin/nxtvepg
/%{prefix}/man/man1/nxtvepg.1
/usr/X11R6/lib/X11/app-defaults/Nxtvepg

%clean
make clean
