#!/usr/bin/perl -w
#
# Script to automatically generate Debian binary package
#
use strict;

my $version = "3.1.0";
my $name = "nxtvepg";
my $arch = "amd64";

mkdir "deb";
mkdir "deb/DEBIAN";
mkdir "deb/usr";
mkdir "deb/usr/share";
mkdir "deb/usr/share/doc";
mkdir "deb/usr/share/doc/$name";
mkdir "deb/usr/share/pixmaps";
mkdir "deb/usr/share/menu";
mkdir "deb/usr/share/applications";
mkdir "deb/usr/share/man";
mkdir "deb/usr/share/man/man1";
mkdir "deb/usr/bin";
mkdir "deb/etc";
mkdir "deb/etc/X11";
mkdir "deb/etc/X11/app-defaults";

# -----------------------------------------------------------------------------
# deb/DEBIAN/control
#
open(CTRL, ">deb/DEBIAN/control") || die;
print CTRL <<EoF;
Package: nxtvepg
Version: $version
Architecture: $arch
Maintainer: T. Zoerner <tomzo\@users.sourceforge.net>
Homepage: https://nxtvepg.sourceforge.net/
Installed-Size: 2832
Depends: libc6 (>= 2.7-1), libstdc++6, libx11-6, libxmu6, tcl8.6 (>= 8.6.0), tk8.6 (>= 8.6.0)
Section: utils
Priority: optional
Description: XMLTV EPG browser & Teletext EPG grabber
 nxtvepg is a browser for TV programme schedules (EPG) stored in files in XMLTV
 format. Additionally, nxtvepg features an integrated Teletext EPG grabber,
 which allows automatically extracting TV schedules from programme tables in
 Teletext of a given list of channels and merging and browsing them immediately
 in the nxtvepg GUI, or exporting them to XMLTV format for use in another
 browser.
 .
 As of 2021, nxtvepg's Teletext EPG grabber supports extracting TV schedules
 from more than 40 TV channels in Germany, providing schedules for 2-10 days.
 Teletext is received either via Digital TV (DVB), or via analog capture
 cards (V4L2).
EoF
close(CTRL);

# -----------------------------------------------------------------------------
# deb/DEBIAN/postinst
#
#open(CTRL, ">deb/DEBIAN/postinst") || die;
#print CTRL <<'EoF';
##!/bin/sh
#set -e
## Automatically added by dh_installmenu
#if [ "$1" = "configure" ] && [ -x "`which update-menus 2>/dev/null`" ]; then
#	update-menus
#fi
## End automatically added section
#EoF
#close(CTRL);
#chmod 0755, "deb/DEBIAN/postinst" || die;

# -----------------------------------------------------------------------------
# deb/DEBIAN/postrm
#
#open(CTRL, ">deb/DEBIAN/postrm") || die;
#print CTRL <<'EoF';
##!/bin/sh
#set -e
## Automatically added by dh_installmenu
#if [ -x "`which update-menus 2>/dev/null`" ]; then update-menus ; fi
## End automatically added section
#EoF
#close(CTRL);
#chmod 0755, "deb/DEBIAN/postrm" || die;

# -----------------------------------------------------------------------------
# deb/DEBIAN/conffiles
#
open(CTRL, ">deb/DEBIAN/conffiles") || die;
print CTRL <<EoF;
/etc/X11/app-defaults/Nxtvepg
EoF
close(CTRL);

# -----------------------------------------------------------------------------
# deb/usr/share/doc/nxtvepg/copyright
#
open(CTRL, ">deb/usr/share/doc/$name/copyright") || die;
print CTRL <<EoF;
Copyright (C) 2006-2011,2020-2023 T. Zoerner. All rights reserved.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

For a copy of the GNU General Public License see:
/usr/share/common-licenses/GPL-3

Alternatively, see <http://www.gnu.org/licenses/>.
EoF
close CTRL;

# -----------------------------------------------------------------------------
# deb/usr/share/menu/nxtvepg
#
#open(CTRL, ">deb/usr/share/menu/nxtvepg") || die;
#print CTRL <<'EoF';
#?package(nxtvepg):needs="x11" section="Applications/Viewers" title="nxtvepg" command="/usr/bin/nxtvepg" icon="/usr/share/pixmaps/nxtvepg.xpm"
#EoF
#close(CTRL);

# -----------------------------------------------------------------------------
# deb/usr/share/applications/nxtvepg.desktop
#
open(CTRL, ">deb/usr/share/applications/nxtvepg.desktop") || die;
print CTRL <<EoF;
[Desktop Entry]
Encoding=UTF-8
Name=nxtvepg
Comment=Lookup the Electronic Program Guide (EPG) from your TV Card
Exec=nxtvepg
Icon=nxtvepg
Terminal=false
Type=Application
Categories=Application;AudioVideo;Player;
StartupNotify=true
EoF
close(CTRL);

# -----------------------------------------------------------------------------

# copy doc files
system "gzip -n -9 -c README.md > deb/usr/share/doc/$name/README.md.gz";
system "gzip -n -9 -c README.tcltk > deb/usr/share/doc/$name/README.tcltk.gz";
system "gzip -n -9 -c CHANGES > deb/usr/share/doc/$name/changelog.gz";
system "gzip -n -9 -c TODO > deb/usr/share/doc/$name/TODO.gz";
system "gzip -n -9 -c manual-de.html > deb/usr/share/doc/$name/manual-de.html.gz";
system "gzip -n -9 -c manual.html > deb/usr/share/doc/$name/manual.html.gz";

# copy system files
die "manual page missing - 'make' not run through?" unless -e "nxtvepg.1";
system "cp", "images/nxtvepg.xpm", "deb/usr/share/pixmaps/nxtvepg.xpm";
system "cp", "Nxtvepg.ad", "deb/etc/X11/app-defaults/Nxtvepg";

# copy & compress manual page
system "gzip -n -9 -c nxtvepg.1 > deb/usr/share/man/man1/nxtvepg.1.gz";
system "gzip -n -9 -c nxtvepgd.1 > deb/usr/share/man/man1/nxtvepgd.1.gz";

# copy & strip executables
die "executable missing - forgot to run 'make all'?" unless -e "build-x86_64/nxtvepgd";
system qw(cp build-x86_64/nxtvepg deb/usr/bin/nxtvepg);
system qw(cp build-x86_64/nxtvepgd deb/usr/bin/nxtvepgd);
system qw(strip deb/usr/bin/nxtvepg);
system qw(strip deb/usr/bin/nxtvepgd);

# build package
system "cd deb; find usr -type f | xargs md5sum > DEBIAN/md5sums";
system "fakeroot dpkg-deb --build deb ${name}_${version}_${arch}.deb";
system "lintian ${name}_${version}_${arch}.deb";
