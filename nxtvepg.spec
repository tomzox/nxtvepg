%define    version   2.9.0

%define    prefix      /usr
%define    res_prefix  /usr/share/X11

%define    debug_package %{nil}

Summary:   nexTView EPG decoder and browser
Name:      nxtvepg
Version:   %{version}
Release:   0
Requires:  tcl >= 8.5, tk >= 8.5
Source0:   nxtvepg-%{version}.tar.gz
Group:     Applications/Multimedia
License:   GPL
URL:       http://prdownloads.sourceforge.net/nxtvepg/nxtvepg-%{version}.tar.gz

%description
The nxtvepg EPG software package supports receiving and browsing Nextview EPG
on your PC. Nextview is an ETSI standard for transmission of Electronic TV
Program Guides within (analog) TV broadcasts. Compared to Teletext with its
pre-formatted schedule tables, Nextview is more flexible due to being based on
a database, which allows for flexible searches; also the database covers
schedules of most or all TV networks at once.

This free service was offered by several content providers in Germany, Austria,
Switzerland, France, Belgium and Turkey up to apx. 2011. Together they covered
the daily TV schedules of all major networks in these countries. However,
Nextview EPG is not transmitted via digital TV signals (DVB), as DVB uses a
different standard with similar features. That standard is not supported by
this software. Therefore, as analog TV transmission has been stopped by now,
the nxtvepg software is mostly of historical interest anymore. For more
historic details see the Nextview EPG service description at the nxtvepg
homepage.

Still useful features of this software are the integrated or standalone
Teletext grabber, which allows extracting TV schedules from programme tables in
Teletext and browsing them in the nxtvepg GUI, or exporting them to XMLTV
format for use in another browser. Inversely, nxtvepg can be used for browsing
data obtained in XMLTV format from other sources.

%prep
%setup

%pre

%post

%build
make ROOT="$RPM_BUILD_ROOT" prefix="%{prefix}" res_prefix="%{res_prefix}" all

%install
make ROOT="$RPM_BUILD_ROOT" prefix="%{prefix}" res_prefix="%{res_prefix}" install

%files
%defattr(-,root,root)
%doc README CHANGES COPYRIGHT TODO manual.html manual-de.html
#%dir /var/tmp/nxtvdb
#%attr(777,root,root) /var/tmp/nxtvdb
/%{prefix}/bin/nxtvepg
/%{prefix}/bin/nxtvepgd
/%{prefix}/share/man/man1/nxtvepg.1.gz
/%{prefix}/share/man/man1/nxtvepgd.1.gz
/%{prefix}/share/pixmaps/nxtvepg.xpm
/%{prefix}/share/nxtvepg/xmltv-etsi.map
/usr/share/X11/app-defaults/Nxtvepg

%clean
rm -rf $RPM_BUILD_ROOT
