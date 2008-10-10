%define    prefix    /usr/local
%define    version   2.8.1

Summary:   nexTView EPG decoder and browser
Name:      nxtvepg
Version:   %{version}
Release:   0
Requires:  tcl >= 8.4, tk >= 8.4
Source0:   nxtvepg-%{version}.tar.gz
Group:     Applications/Multimedia
License:   GPL
URL:       http://prdownloads.sourceforge.net/nxtvepg/nxtvepg-%{version}.tar.gz
BuildRoot: /tmp/nxtvepg-build

%description
nxtvepg allows to receive, analyze and browse TV programme schedules
transmitted on top of Teletext as defined by the European
Telecommunications Standards Institute (ETSI) in ETS 300 707:
"Protocol for a TV Guide using electronic data transmission".

As of September 2008 the following Nextview EPG providers are available:

- for Germany and Austria: Kabel1 (coverage: apx. 32 networks)
- for Switzerland: SF1, TSR1, TSI1, TV5 (coverage: apx. 37 networks)
- for France: Canal+, M6, TV5 (coverage: 8 networks)
- for Belgium: M6, TV5 (coverage: 32 networks)
- for Turkey: TRT family (coverage: apx. 17 networks)

If you don't receive any of those, then this software unfortunately is
almost useless to you, except for the possibility to acquire EPG data
from teletext and external sources via XMLTV (but those are mainly
designed to accompany Nextview EPG so they won't work very well
stand-alone.)  For more details on pre-requesites please refer to
the documentation in the "Help" menus and manual page.

%prep
%setup

%build
make ROOT="$RPM_BUILD_ROOT" prefix="%{prefix}" daemon

%install
make ROOT="$RPM_BUILD_ROOT" install

%files
%defattr(-,root,root)
%doc README CHANGES COPYRIGHT TODO manual.html manual-de.html
%dir /var/tmp/nxtvdb
%attr(777,root,root) /var/tmp/nxtvdb
/%{prefix}/bin/nxtvepg
/%{prefix}/bin/nxtvepgd
/%{prefix}/man/man1/nxtvepg.1
/%{prefix}/man/man1/nxtvepgd.1
/%{prefix}/share/nxtvepg/xmltv-etsi.map
/%{prefix}/share/nxtvepg/tv_grab_ttx.pl
/etc/X11/app-defaults/Nxtvepg

%clean
make clean
