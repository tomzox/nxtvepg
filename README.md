# nxtvepg - XMLTV EPG browser & Teletext EPG grabber

nxtvepg is a browser for TV programme schedules (EPG) stored in XMLTV format.
nxtvepg supports flexible merging EPG from multiple sources and displaying the
result in a compact table view, or a grid view. It supports filtering the TV
schedule based on a large number of attributes, a filter shortcut list in the
main window, adding custom columns to the view to mark programmes matching
predefined filters, and reminders for individual programmes or filter
shortcuts.

nxtvepg also features an integrated Teletext EPG grabber, which uses a TV
capture card for automatically extracting TV schedules from programme tables in
Teletext from a given list of channels. Acquired EPG data can be merged and
browsed immediately in the nxtvepg GUI, or exported to files in XMLTV format
for use in another browser. Currently, only German TV networks are supported
well by the grabber; support for other networks can be added on request,
provided they transmit Teletext with usable TV schedules.

*Historically*, nxtvepg was developed for receiving and browsing Nextview
EPG. Nextview was an European standard for transmission of Electronic TV
Program Guides within analog TV broadcasts. However, as the world has switched
to digital TV broadcast since, it is no longer transmitted today. Therefore,
support for this standard has been removed from nxtvepg in release 3.0.

For a comprehensive description of features of this software, please refer
to the manual page: In the source package, see [nxtvepg.pod](./nxtvepg.pod);
after installation, type `man nxtvepg` in a shell (UNIX only), or open
`manual.html` (English language) or `manual-de.html` (German) included in the
pre-compiled package in a Web browser, or open
<http://nxtvepg.sourceforge.net/man.html>

## System requirements

For browsing XMLTV files, any PC sold in the past 20 years will do.

For the Teletext EPG grabber, a DVB or analog TV tuner card is required
and you need to be able to receive a network that transmits Teletext
service with programme tables.

The Linux version supports all Digital TV (DVB) cards and analog cards
for which a v4l2 ("video 4 linux, version 2") compatible driver exists
that supports teletext reception via /dev/vbi (e.g. bttv, saa7134,
cx8800, possibly even USB TV boxes).  NetBSD and FreeBSD versions
supports all cards which are supported by the bktr driver.

The MS Windows version supports only analog TV cards that are supported via
the separately provided WDM driver interface DLL `VbiAcqWdmDrv.dll`.
Pre-requisite for that is a vendor-provided WDM driver module that supports
"VBI" decoding (e.g. teletext). Digital TV cards (DVB) ard currently not
supported for MS Windows.

### Linux software requirements

- For the Teletext EPG grabber (optional): Either V4L2 drivers (i.e. video for
  Linux, API 2) when using analog TV capture cards, or Digital TV drivers for
  DVB capture cards.
- When compiling from source: GNU C++ Compiler with support for C++14.
  You'll also need development (i.e. "-dev") versions of various
  packages such as "xorg" so that header files are available.
- Tcl/Tk version 8.5 or 8.6.
  Sources available for download from <http://www.tcl.tk/>
- Any release of X11R5 or X11R6
  Note: X11 and Tcl/Tk are not required if you only build the daemon

### NetBSD software requirements

- kernel >= 1.5 including the bktr driver 2.17 or later,
  available from <http://vulture.dmem.strath.ac.uk/bt848/>
- other dependencies are same as for Linux.

### Windows software requirements

- For the Teletext EPG grabber (optional): A WDM driver for your analog
  TV card that supports VBI decoding. Use of the WDM driver (based on
  DirectShow) requires DirectX 9 or later.
- Windows 95: the winsock2 DLL is required (`ws2_32.dll`, for the daemon
  feature) which - in contrary to newer Windows versions - was not included
  with this OS.
- All Windows versions starting with Windows 95 are supported.

Note the Windows version currently does *not* support Digital TV capture
cards.


## UNIX installation procedure (when compiling from source)

- Optional, for Teletext EPG grabber only: first install a teletext decoder
  application (e.g. `aleVT`) to verify that your TV card drivers work correctly.

- There's currently no "configure" script, so you have to manually check
  path definitions at the top of the Makefile, esp. for the Tcl/Tk libraries,
  include path and script libraries.

- There are also several compile-time options in the Makefile which you
  might want to consider, however the defaults should work everywhere.

- If you compile on a operating system other than Linux, BSD or Windows,
  then replace use of `epgvbi/btdrv4linux` in the Makefile with
  `epgvbi/btdrv4dummy`. This will disable EPG acquisition via the TV card, but
  you still can use network acquisition mode to receive data from a supported
  platform.

- `make`:
    * Note: typing `make nxtvepg` will not work since there are other targets
      which need to be built first.  Hence just start make without parameters.
    * If compilation fails with massive syntax errors, check the type
      definitions in epgctl/mytypes.h first.  Required types are: bool,
      schar, uchar, sint, uint, slong, ulong.  Compilation may fail if these
      are already defined in system header files; in this case you probably
      have to comment out nxtvepg's definition.
    * If compilation fails due to missing "include files", you probably lack
      development versions of standard libraries. On Debian/Ubuntu you need:
      `apt-get install tcl8.6 tcl8.6-dev tk8.6 tk8.6-dev`
      `apt-get install libx11-dev libxmu6 libxmu-dev`
      For Fedora and other RPM-based systems you need:
      `yum install tcl-devel tk-devel libXmu-devel perl-Pod-Html`
    * To build the "nxtvepgd" executable type `make daemon` or `make all`.
      This will compile one extra module and otherwise just link a smaller
      executable which does not contain the graphical user interface.
    * To compile the tvsim test & development suite too, invoke `make all`
      (Note these tools are not installed into system directories since they
      are intended for temporary use in debugging only.)
    * Intermediate objects and the binaries are generated in a subdirectory
      named `build-<platform>` (e.g. build-i386 for an Intel compatible)

- `make install`:
  This will copy the nxtvepg and nxtvepgd executables and their respective
  manual pages into the system directories configured in the Makefile
  via variable `prefix`. Usually you need to "su root" to have permission
  to do so.

- Invoke the build executable build/nxtvepg. For browsing XMLTV files,
  simply add their path on the command line to load and display their
  content. When started without arguments nxtvepg will initialls show an
  screen with the nxtvepg logo and a small help text. Follow the help to
  enable the Teletext EPG grabber, or read manual chapter "Getting
  Started".

If the GUI shows up but does not react to any input, you probably
need to tell Tcl/Tk where to find its libraries. To do so, correct
the paths in `TCL_LIBRARY_PATH` and `TK_LIBRARY_PATH` in the Makefile.
If you need to change anything besides the Makefile, please let me
know about it (i.e. send me the diffs)


## Windows installation procedure (for pre-compiled binary package)

- Unpack the zip file into an empty directory.

- When intending to use the Teletext EPG grabber: Make sure you've stopped
  your TV viewing application and any other video applications, else
  nxtvepg willl not be able to load data, because only one application
  may use the TV card at the same time.)

- Invoke the executable nxtvepg.exe

    * For browsing pre-existing XMLTV files, simply load them via the
      Control menu.
    * For acquiring Teletext EPG, set up the TV card driver via the
      "TV card input" and then enable the "Teletext grabber". Both
      dialogs can be found in the Configuration menu.

### Compilation from source for Windows

- Note this is NOT required for regular releases for which a pre-compiled
  binary release is provided. This is required only if you downloaded the
  latest SW version from github, or if you want to change the software.
- Fastest way is to cross-compile for Windows under Linux.  You need to
  install "mingw32" package and then type `make -f Makefile.win`
- Alternatively you can install GNU/Cygwin on your Windows host, see
  <http://www.cygwin.com/>.  Most important are packages for make, gcc,
  mingw ("Minimalistic GNU for Windows") and win32 header files.  After
  installing Cygwin, start a shell, cd to the nxtvepg directory and type
  "make".  If you're not using NT or W2K you may have to adapt the OS
  detection code in Makefile and/or Makefile.win, i.e.
  `ifeq ($(OS),Windows_NT)`
- To generate the help menus from the documentation in POD format you
  need Perl5 (not essential unless you want to modify the help menus)
  from <http://www.perl.org/> or as Cygwin package


## Problem and bug reports

Before you email me any bug reports, please check the manual page as well
as the TODO and CHANGES files. And be *comprehensive* in describing your
problem. At least include names and version numbers of your operating system,
nxtvepg, possibly the bttv driver and any other software that may be
involved. If it looks like a hardware related problem (e.g. acquisition
does not work at all) also include the exact name of you TV card hardware.
If DScaler or K!TV work with the same hardware, mail me the version number and
describe your hardware configuration settings. If there is an error message,
quote the message in full length and exactly as printed, even if you do not
understand its relevance.

In case nxtvepg crashes, or if you suspect internal errors, you need to
recompile with debug support to allow me to analyze the problem.  Please
add the -g option to CFLAGS in the Makefile and enable all debug options in
mytypes.h by assigning ON instead of OFF. If you know which modules are
responsible, you can also comment out `DPRINTF_OFF` at the top of those
modules. Then try to reproduce your problem and send me the resulting
debug.out log file.

The latest version of this software is available at:
<http://nxtvepg.sourceforge.net/>

Best way to provide feedback is via the discussion forum at the above site,
but you can also contact me personally at: tomzo (at) users.sourceforge.net

have fun.

-tom
