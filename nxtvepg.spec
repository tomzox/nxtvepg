%define    version   3.1.0

%define    prefix      /usr
%define    res_prefix  /usr/share/X11

%define    debug_package %{nil}

Summary:   XMLTV EPG browser & Teletext EPG grabber
Name:      nxtvepg
Version:   %{version}
Release:   0
Requires:  tcl >= 8.6, tk >= 8.6
Source0:   nxtvepg-%{version}.tar.gz
Group:     Applications/Multimedia
License:   GPL
URL:       http://prdownloads.sourceforge.net/nxtvepg/nxtvepg-%{version}.tar.gz

%description
nxtvepg is a browser for TV programme schedules (EPG) stored in files in XMLTV
format. Additionally, nxtvepg features an integrated Teletext EPG grabber,
which allows automatically extracting TV schedules from programme tables in
Teletext of a given list of channels and merging and browsing them immediately
in the nxtvepg GUI, or exporting them to XMLTV format for use in another
browser.

As of 2021, nxtvepg's Teletext EPG grabber supports extracting TV schedules
from more than 40 TV channels in Germany, providing schedules for 2-10 days.
Teletext is received either via Digital TV (DVB), or via analog capture
cards (V4L2).

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
%doc README.md CHANGES COPYRIGHT TODO manual.html manual-de.html
/%{prefix}/bin/nxtvepg
/%{prefix}/bin/nxtvepgd
/%{prefix}/share/man/man1/nxtvepg.1.gz
/%{prefix}/share/man/man1/nxtvepgd.1.gz
/%{prefix}/share/pixmaps/nxtvepg.xpm
/usr/share/X11/app-defaults/Nxtvepg

%clean
rm -rf $RPM_BUILD_ROOT
