#!/usr/bin/perl -w
#
#  This script captures teletext from a VBI device, scrapes TV programme
#  schedules and descriptions from teletext pages and exports them in
#  XMLTV format (DTD version 0.5)
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#  Copyright 2006-2008 by Tom Zoerner (tomzo at users.sf.net)
#
#  $Id: tv_grab_ttx.pl,v 1.22 2008/10/05 18:33:19 tom Exp tom $
#

use POSIX;
use locale;
use Time::Local;
use strict;

my $version = 'Teletext EPG grabber, v1.1';
my $copyright = 'Copyright 2006-2008 Tom Zoerner';
my $home_url = 'http://nxtvepg.sourceforge.net/tv_grab_ttx';

my %Pkg;
my %PkgCni;
my %PgCnt;
my %PgSub;
my %PgLang;
my %PageCtrl;
my %PageText;
my $VbiCaptureTime;

# command line options for capturing from device
my $opt_duration = 0;
my $opt_device = "/dev/vbi0";
my $opt_dvbpid = undef;
my $opt_verbose = 0;
my $opt_debug = 0;

# command line options for grabbing
my $opt_infile;
my $opt_outfile;
my $opt_mergefile;
my $opt_chname;
my $opt_chid;
my $opt_dump = 0;
my $opt_verify = 0;
my $opt_tv_start = 0x301;
my $opt_tv_end = 0x399;
my $opt_expire = 120;

# ------------------------------------------------------------------------------
# Command line argument parsing
#
sub ParseArgv {
   my $usage = "Usage: $0 [OPTIONS] [<file>]\n".
               "  -device <path>\t: VBI device used for input (when no file given)\n".
               "  -dvbpid <PID>\t\t: Use DVB stream with given PID\n".
               "  -page <NNN>-<MMM>\t: Page range for TV schedules\n".
               "  -chn_name <name>\t: display name for XMLTV \"<channel>\" tag\n".
               "  -chn_id <name>\t: channel ID for XMLTV \"<channel>\" tag\n".
               "  -outfile <path>\t: Redirect XMLTV or dump output into the given file\n".
               "  -merge <path>\t\t: Merge output with programmes from file\n".
               "  -expire <N>\t\t: Omit programmes which ended X minutes ago\n".
               "  -dumpvbi\t\t: Dump captured VBI data; no grabbing\n".
               "  -dump\t\t\t: Dump teletext pages as clear text; no grabbing\n".
               "  -dumpraw <NNN>-<MMM>\t: Dump teletext packets in given range; no grabbing\n".
               "  -verify\t\t: Load previously dumped \"raw\" teletext packets\n".
               "  -verbose\t\t: Print teletext page numbers during capturing\n".
               "  -debug\t\t: Emit debug messages during grabbing and device open\n".
               "  -version\t\t: Print version number only, then exit\n".
               "  -help\t\t\t: Print this text, then exit\n".
               "  file or \"-\"\t\t: Read VBI input from file instead of device\n";

   while ($_ = shift @ARGV) {
      # -chn_name: display name in XMLTV <channel> tag
      if (/^-chn_name$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_chname = shift @ARGV;

      # -chn_name: channel ID in XMLTV <channel> tag
      } elsif (/^-chn_id$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_chid = shift @ARGV;

      # -page <NNN>-<MMM>: specify range of programme overview pages
      } elsif (/^-pages?$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $_ = shift @ARGV;
         if (m#^([0-9]{3})\-([0-9]{3})$#) {
            $opt_tv_start = hex($1);
            $opt_tv_end = hex($2);
            die "-page: not a valid start page number: $opt_tv_start" if (($opt_tv_start < 0x100) || ($opt_tv_start > 0x899));
            die "-page: not a valid end page number: $opt_tv_end" if (($opt_tv_end < 0x100) || ($opt_tv_end > 0x899));
            die "-page: start page must be <= end page" if ($opt_tv_end < $opt_tv_start);
         } else {
            die "-page: invalid parameter: \"$_\", expecting two 3-digit ttx page numbers\n";
         }

      # -outfile <name>: specify output file (to replace use of stdout)
      } elsif (/^-o(utfile)?$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_outfile = shift @ARGV;
         die "Output file $opt_outfile already exists\n" if -e $opt_outfile;

      # -merge <name>: specify XMLTV input file with which to merge grabbed data
      } elsif (/^-merge$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_mergefile = shift @ARGV;

      # -expire <hours>: skip (or remove in merge) programmes older than X minutes
      } elsif (/^-expire$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_expire = shift @ARGV;
         die "$_ requires a numerical argument\n$usage" unless $opt_expire =~ /^\d+$/;

      # -debug: enable debug output
      } elsif (/^-debug$/) {
         $opt_debug = 1;

      # -dump: print all teletext pages, then exit (for debugging only)
      } elsif (/^-dump$/) {
         $opt_dump = 1;

      # -dumpraw: write teletext pages in binary, suitable for verification
      } elsif (/^-dumpraw$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_dump = 2;
         $_ = shift @ARGV;
         if (m#^([0-9]{3})\-([0-9]{3})$#) {
            $opt_tv_start = hex($1);
            $opt_tv_end = hex($2);
         } else {
            die "-dumpraw: invalid parameter: \"$_\", expecting two 3-digit ttx page numbers\n";
         }

      # -dump: write all teletext and VPS packets to file
      } elsif (/^-dumpvbi$/) {
         $opt_dump = 3;

      # -verify: read pre-processed teletext from file (for verification only)
      } elsif (/^-verify$/) {
         $opt_verify = 1;

      # -duration <seconds|"auto">: stop capturing from VBI device after the given time
      } elsif (/^-duration$/) {
         die "$0: missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_duration = shift @ARGV;
         die "$0: $_ requires a numeric argument\n" unless $opt_duration =~ m#^[0-9]+$#;
         die "$0: Invalid -duration value: $opt_duration\n" unless $opt_duration > 0;

      # -dev <device>: specify VBI device
      } elsif (/^-dev(ice)?$/) {
         die "$0: missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_device = shift @ARGV;
         die "$0: -dev $opt_device: doesn't exist\n" unless -e $opt_device;
         die "$0: -dev $opt_device: not a character device\n" unless -c $opt_device;

      # -dvbpid <number>: capture from DVB device from a stream with the given PID
      } elsif (/^-dvbpid$/) {
         die "$0: missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_dvbpid = shift @ARGV;
         die "$_ requires a numeric argument\n" if $opt_dvbpid !~ m#^[0-9]+$#;

      # -verbose: print page numbers during capturing, progress info during grabbing
      } elsif (/^-verbose$/) {
         $opt_verbose = 1;

      } elsif (/^-version$/) {
         print STDERR "$version\n$copyright" .
                      "\nThis is free software with ABSOLUTELY NO WARRANTY.\n" .
                      "Licensed under the GNU Public License version 3 or later.\n" .
                      "For details see <http://www.gnu.org/licenses/\n";
         exit 0;

      # -help: print usage (i.e. command line syntax) and exit
      } elsif (/^-(help|h|\?)$/) {
         print STDERR $usage;
         exit 0;

      # sole "-": read input data from STDIN
      } elsif (/^-$/) {
         $opt_infile = "";
         last;

      } elsif (/^-/) {
         die "$_: unknown argument \"$_\"\n$usage";

      } else {
         $opt_infile = $_;
         die "$0: no more than one input file expected.\n" .
             "Error after '$opt_infile'\n$usage" unless $#ARGV <= 0;
      }
   }
}

=head1 NAME

tv_grab_ttx - Grab TV listings from teletext through a TV card

=head1 SYNOPSIS

tv_grab_ttx [options] [file]

=head1 DESCRIPTION

This EPG grabber allows to extract TV programme listings in XMLTV
format from teletext pages as broadcast by most European TV networks.
The grabber works by collecting teletext pages through a TV card,
locating pages holding programme schedule tables and then scraping
starting times, titles and attributes from those. Additionally,
the grabber follows references to further teletext pages in the
overviews to extract description texts. The grabber needs to be
started separately for each TV channel from which EPG data shall be
collected.

Teletext data is captured directly from I</dev/vbi0> or the device
given by the I<-dev> command line parameter, unless an input I<file>
is named on the command line. In case of the latter, the grabber expects
a stream of pre-processed VBI packets as generated when using
option I<-dumpvbi>.  Use file name C<-> to read from standard input
(i.e. from a pipe.)

The XMLTV output is written to standard out unless redirected to a
file by use of the I<-outfile> option. There are also "dump" options
which allow to write each VBI packet or assembled teletext input
pages in raw or clear text format. See L<"OPTIONS"> for details.

The EPG output generated by this grabber is compatible to all
applications which can process XML files which adhere to XMLTV
DTD version 0.5. See L<http://xmltv.org/> for a list of applications
and a copy of the DTD specification. In particular, the output can
be imported into L<nxtvepg(1)> and merged with I<Nextview EPG>.

=head1 OPTIONS

=over 4

=item B<-page> NNN-MMM

This limits the range of the teletext page numbers which are extracted
from the input stream. At the same time this defines the range of pages
which are scanned for programme schedule tables. The default page range
is 300 to 399.

=item B<-outfile> path

This option can be used to redirect the XMLTV output into a file.
By default the output is written to standard out.

=item B<-chn_name> name

This option can be used to set the value of the I<display-name> tag
in the channel table of the generated XMLTV output. By default the
name is extracted from teletext page headers.

=item B<-chn_id> id

This option can be used to set the value of the channel I<id> attribute
in the channel table of the generated XMLTV output.

By default the identifier is derived from the canonical network
identifier (CNI) which is broadcast as part of VPS and PDC.
The grabber uses an internal table to map these numerical values
to strings in the form suggested by RFC2838 (e.g. CNI C<1DC1>
is mapped to C<ard.de>)

=item B<-merge> file

This option can be used to merge the newly grabber data with previously
grabbed data, or with data grabbed from other channels.

B<Caution:> This option only allows to merge input files which have
been written by the same version of the grabber. This is because
the grabber does not include a complete XMLTV parser, so the input
is expected in the exact format in which the grabber has written it.

=item B<-expire> minutes

This option can be used to omit programmes which have completed
more than the given number of minutes ago. Note for programmes
for which the stop time is unknown the start time plus 120 minutes
is used. By default the expire time is 120 minutes.

This option is particularily intended for use together with
the I<-merge> option.

=item B<-dumpvbi>

This option can be used to omit almost all processing after
digitization ("slicing") of VBI data and write incoming teletext and
VPS packets to the output. The output can later be read by the grabber
as input file.

In the output, each data record consists of 46 bytes of which the first
two contain the magazine and page number (0x000..0x7FF), the next three
the header control bits including sub-page number, the next byte the
packet number (0 for page header, 1..29 for teletext packets, 30 for
PDC/NI packets, 32 for VPS) and finally the last 40 bytes the payload
data (only 16-bit CNI value in case of VPS/PDC/NI) Note the byte-order
of 16-bit values is platform-dependent.

=item B<-dump>

This option can be used to omit grabbing and instead print all
teletext pages received in the input stream as text
in Latin-1 encoding. Note control characters and characters which
cannot be represented in Latin-1 are replaced with spaces. This
option is intended for debugging purposes only.

=item B<-dumpraw> NNN-MMM

This option can be used to disable grabbing and instead write all
teletext pages in the given page range to the output stream in a
specific format which can later be imported again via option I<-verify>.
The output includes the raw teletext packets for each page including
all control characters. Additionally, the output contains VPS/PDC
packets and a timestamp which indicates the capture time. (The latter
is required when parsing relative time and date specifications.)

=item B<-verify>

This option can be used to read input data in the format as written
by use of option I<dumpraw> instead of the standard VBI packet stream.
As the name indicates, this mode is used in regression testing to
compare the grabber output for pre-defined input page sequences with
stored output files.

=item B<-dev> I<path>

This option can be used to specify the VBI device from which teletext
is captured. By default F</dev/vbi0> is used.

=item B<-dvbpid> I<PID>

This option enables data acquisition from a DVB device and specifies
the PID which is the identifier of the data stream which contains
teletext data. The PID must be determined by external means. Likewise
to analog sources, the TV channel also must be tuned with an external
application.

=item B<-duration> I<seconds>

This option can be used to limit capturing from the VBI device to the
given number of seconds. EPG grabbing will start afterwards.
If this option is not present, capturing stops automatically after a
sufficient number of pages have been read.  
This option has no effect when reading VBI data from a file.

=item B<-verbose>

This option can be used to enable output of the number of each
teletext page when capturing from a VBI device. This allows monitoring
the capture progress.

=item B<-debug>

This option can be used to enable output of debugging information
to standard output. You can use this to get additional diagnostic
messages in case of trouble with the capture device. (Note most of
these messages originate directly from the "ZVBI" library I<libzvbi>.)
Additionally this option enables messages during parsing teletext
pages; these are probably only helpful for developers.

=item B<-version>

This option prints version and license information, then exits.

=item B<-help>

This option prints the command line syntax, then exits.

=back

=head1 GRABBING PROCESS

This chapter describes the process in which EPG information is extracted
from teletext pages. This information is not needed to use the grabber
but may help you to understand the goals and limitations. The main
challenge of the grabber is that source data is not formatted
consistently. Formats used for tables, dates and other attributes may
vary widely between networks and may also vary over time or even
between pages in a cycle for a specific network. So far the grabber
avoids any network-specific hacks and instead attempts to address
the problem algorithmically.

Before starting the actual grabbing, all teletext packets are read
from the input file or stream into memory and associated with
teletext page numbers. In case the same packet is received more than
once, the one with the lower parity error count is selected.

The first step of grabbing is identification of the overview pages
which contain tables with start times and programme titles. This is
done by searching all pages for lines starting with a time value,
but allowing for exceptions such as markers or hidden VPS labels:

   20.00  Tagesschau UT ............ 310
 ! 20.15  Die Stein (10/13) UT ..... 324
          Zwischen Baum und Borke
          Fernsehserie, D 2008
 ! 21.05  In aller Freundschaft UT . 326
          Grossmuetiges Herz (D, 2008)
 ! 21.50  Plusminus UT ............. 328

Of course there may be unrelated rows which also start with a
time value. Hence the grabber builds statistics about the number
of occurences of lines where the time and title start in the same
column. In the end, the format found most often is selected as the
one used for detecting time tables.

In the second step, the grabber revisits all pages and extracts
start times and titles from all lines which are formatted in the
way selected in step 1 above. Additionally, the title lines are 
broken apart into time, title, episode title, feature attributes
(stereo, sub-titles etc.), description page references. Afterwards
stop times are calculated by asserting the start of subsequent
programmes as the stop time of the previous one.

Finally the date is derived. In some cases the data is printed in
full on top of the page, but often the data is just given as "Today",
"Tomorrow" etc. or a weekday name. Even more difficult, a day on
these pages sometimes is considered to span until apx. 6:00 on
the next day, so finding "today" after midnight may actually mean
"yesterday".  The latter is resolved by comparing dates between
adjacent pages.

Format of dates is understood in many different formats. Examples:

  Heute
  29.04.06
  Sa 29.04.06
  Sa,29.04.
  Samstag, 29.April
  Sonnabend, 29.04.06
  29.04.2006

In the third step, descriptions are grabbed from all pages which
were referenced by the overview tables. Often, different sub-pages
of the same page are used for descriptions belonging to different
programmes. Hence the grabber must make a match between the
referencing programmes start time and title and the content on
the description page to identify the correct sub-page. Often this
gets difficult when repetitions of an episode refer to the same
description page (most often without listing only the original
air date). Example:

  Relic Hunter - Die Schatzj{gerin
  Der magische Handschuh
  
  Sa 06.05 18:01-18:59           (vorbei)
  So 07.05 05:10-05:57           (vorbei)

In the final step, the channel name and identifier are derived from
teletext page headers and channel identification codes. Once more
statistics are used to filter out transmission errors. Since the
teletext page header usually contains additional text after the
actual network name (e.g. "ARDtext Sa 10.06.06" instead of "ARD")
the name is derived by stripping all appendices which match known
time/date formats or other common postfixes.

The data collected in all the above steps is then formatted in XMLTV,
optionally merged with an input XMLTV file, and printed to the output
file.

=head1 FILES

=over 4

=item B</dev/vbi0>, B</dev/v4l/vbi0>

Device files from which teletext data is being read during acquisition
when using an analog TV card on Linux.  Different paths can be selected
with the B<-dev> command line option.  Depending on your Linux version,
the device files may be located directly beneath C</dev> or inside
C</dev/v4l>. Other operating systems may use different names.

=item B</dev/dvb/adapter0/demux0>

Device files from which teletext data is being read during acquisition
when using a DVB device, i.e. when the B<-dvbpid> option is present.
Different paths can be selected with the B<-dev> command line option.
(If you have multiple DVB cards, increment the device index after
I<adapter> to get the second card etc.)

=back

=head1 SEE ALSO

L<xmltv(5)>,
L<tv_cat(1)>,
L<nxtvepg(1)>,
L<v4lctl(1)>,
L<zvbid(1)>,
L<alevt(1)>,
L<Video::ZVBI(3pm)>,
L<perl(1)>

=head1 AUTHOR

Written by Tom Zoerner (tomzo at users.sourceforge.net) since 2006
as part of the nxtvepg project.

The official homepage is L<http://nxtvepg.sourceforge.net/tv_grab_ttx>

=head1 COPYRIGHT

Copyright 2006-2008 Tom Zoerner.

This is free software. You may redistribute copies of it under the terms
of the GNU General Public License version 3 or later
L<http://www.gnu.org/licenses/gpl.html>.
There is B<no warranty>, to the extent permitted by law.

=cut

# ------------------------------------------------------------------------------
# Decoding of teletext packets (esp. hamming decoding)
# - for page header (packet 0) the page number is derived
# - for teletext packets (packet 1..25) the payload data is just copied
# - for PDC and NI (packet 30) the CNI is derived
# - returns a list with 5 elements: page/16, ctrl/16, ctrl/8, pkg/8, data/40*8
# 
sub FeedTtxPkg {
   my ($last_pg, $data) = @_;
   my @ret_buf;

   my $mpag = Video::ZVBI::unham16p($data);
   my $mag = $mpag & 7;
   my $y = ($mpag & 0xf8) >> 3;

   if ($y == 0) {
      # teletext page header (packet #0)
      my $page = ($mag << 8) | Video::ZVBI::unham16p($data, 2);
      my $ctrl = (Video::ZVBI::unham16p($data, 4)) |
                 (Video::ZVBI::unham16p($data, 6) << 8) |
                 (Video::ZVBI::unham16p($data, 8) << 16);
      # drop filler packets
      if (($page & 0xFF) != 0xFF) {
         Video::ZVBI::unpar_str($data);
         #$sub = $ctrl & 0xffff;
         # format: pageno, ctrl bis, ctrl bits, pkgno
         @ret_buf = ($page, ($ctrl & 0xFFFF), ($ctrl >> 16), $y, substr($data, 2, 40));
         if ($opt_dump == 3) {
            syswrite(STDOUT, pack("SSCCa40", @ret_buf));
         }
         my $spg = ($page << 16) | ($ctrl & 0x3f7f);
         if ($spg != $$last_pg) {
            print STDERR sprintf("%03X.%04X\n", $page, $ctrl & 0x3f7f) if $opt_verbose;
            $$last_pg = $spg;
         }
      }

   } elsif ($y<=25) {
      # regular teletext packet (lines 1-25)
      Video::ZVBI::unpar_str($data);
      @ret_buf = (($mag << 8), 0, 0, $y, substr($data, 2, 40));
      if ($opt_dump == 3) {
         syswrite(STDOUT, pack("SSCCa40", @ret_buf));
      }
   } elsif ($y == 30) {
      my $dc = (Video::ZVBI::unham16p($data, 2) & 0x0F) >> 1;
      if ($dc == 0) {
         # packet 8/30/1
         my $cni = Video::ZVBI::rev16p($data, 2+7);
         if (($cni != 0) && ($cni != 0xffff)) {
            @ret_buf = (($mag << 8), 0, 0, $y, $cni);
            if ($opt_dump == 3) {
               syswrite(STDOUT, pack("SSCCSx18x20", @ret_buf));
            }
         }
      } elsif ($dc == 1) {
         # packet 8/30/2
         my $c0 = Video::ZVBI::rev8(Video::ZVBI::unham16p($data, 2+9+0));
         my $c6 = Video::ZVBI::rev8(Video::ZVBI::unham16p($data, 2+9+6));
         my $c8 = Video::ZVBI::rev8(Video::ZVBI::unham16p($data, 2+9+8));

         my $cni = (($c0 & 0xf0) << 8) |
                   (($c0 & 0x0c) << 4) |
                   (($c6 & 0x30) << 6) |
                   (($c6 & 0x0c) << 6) |
                   (($c6 & 0x03) << 4) |
                   (($c8 & 0xf0) >> 4);
         $cni &= 0x0FFF if (($cni & 0xF000) == 0xF000);
         if (($cni != 0) && ($cni != 0xffff)) {
            @ret_buf = (($mag << 8), 0, 0, $y, $cni);
            if ($opt_dump == 3) {
               syswrite(STDOUT, pack("SSCCSx18x20", @ret_buf));
            }
         }
      }
   }
   return @ret_buf;
}

# ------------------------------------------------------------------------------
# Decoding of VPS packets
# - returns a list of 5 elements (see TTX decoder), but data contains only 16-bit CNI
#
sub FeedVps {
   my @ret_buf;
   #my $cni = Video::ZVBI::decode_vps_cni($_[0]); # only since libzvbi 0.2.22

   my $cd_02 = ord(substr($_[0],  2, 1));
   my $cd_08 = ord(substr($_[0],  8, 1));
   my $cd_10 = ord(substr($_[0], 10, 1));
   my $cd_11 = ord(substr($_[0], 11, 1));

   my $cni = (($cd_10 & 0x3) << 10) | (($cd_11 & 0xc0) << 2) |
             ($cd_08 & 0xc0) | ($cd_11 & 0x3f);

   if ($cni == 0xDC3) {
      # special case: "ARD/ZDF Gemeinsames Vormittagsprogramm"
      $cni = ($cd_02 & 0x20) ? 0xDC1 : 0xDC2;
   }
   if (($cni != 0) && ($cni != 0xfff)) {
      @ret_buf = (0, 0, 0, 32, $cni);
      if ($opt_dump == 3) {
         syswrite(STDOUT, pack("SSCCSx18x20", @ret_buf));
      }
   }
   return @ret_buf;
}

# ------------------------------------------------------------------------------
# Open the VBI device for capturing, using the ZVBI library
# - require is done here (and not via "use") to avoid dependency
#
sub DeviceOpen {
   require Video::ZVBI;

   my $srv = &Video::ZVBI::VBI_SLICED_VPS |
             &Video::ZVBI::VBI_SLICED_TELETEXT_B |
             &Video::ZVBI::VBI_SLICED_TELETEXT_B_525;
   my $err;
   my $cap;

   if (defined($opt_dvbpid)) {
      $cap = Video::ZVBI::capture::dvb_new($opt_device, 0, $srv, 0, $err, $opt_debug);
      $cap->dvb_filter($opt_dvbpid) 
   } else {
      $cap = Video::ZVBI::capture::v4l2_new($opt_device, 6, $srv, 0, $err, $opt_debug);
   }

   die "$0: failed to open capture device $opt_device: $err\n" unless $cap;
   return $cap;
}

# ------------------------------------------------------------------------------
# Read one frame's worth of teletext and VPS packets
# - blocks until the device delivers the next packet
# - returns a flat list; 5 elements in list for each teletext packet
#
sub DeviceRead {
   my ($cap, $last_pg) = @_;
   my @ret_buf = ();

   my $rin = "";
   vec($rin, $cap->fd(), 1) = 1;
   select($rin, undef, undef, undef);

   my $buf;
   my $lcount;
   my $ts;
   my $ret = $cap->pull_sliced($buf, $lcount, $ts, 0);
   if (($ret > 0) && ($lcount > 0)) {
      for (my $idx = 0; $idx < $lcount; $idx++) {
         my @tmpl = Video::ZVBI::get_sliced_line($buf, $idx);
         if ($tmpl[1] & (&Video::ZVBI::VBI_SLICED_TELETEXT_B |
                         &Video::ZVBI::VBI_SLICED_TELETEXT_B_525)) {
            push @ret_buf, FeedTtxPkg($last_pg, $tmpl[0]);

         } elsif ($tmpl[1] & &Video::ZVBI::VBI_SLICED_VPS) {
            push @ret_buf, FeedVps($tmpl[0]);
         }
      }

   } elsif ($ret < 0) {
      die "$0: capture device error: $opt_device: $!\n";
   }

   return @ret_buf;
}

# ------------------------------------------------------------------------------
# Read VBI data from a device or file
# - when reading from a file, each record has a small header plus 40 bytes payload
# - TODO: use MIP to find TV overview pages
#
sub ReadVbi {
   my @CurPage = (-1, -1, -1, -1, -1, -1, -1, -1);
   my @CurSub = (0,0,0,0,0,0,0,0,0);
   my @CurPkg = (0,0,0,0,0,0,0,0,0);
   my $cur_mag = 0;
   my $intv;
   my $last_pg = -1;
   my $cap;
   my @CapPkgs;
   my $mag_serial = 0;
   my ($mag, $page, $sub, $ctl1, $ctl2, $pkg, $data);

   if (defined $opt_infile) {
      # read VBI input data from a stream or file
      if ($opt_infile eq "") {
         open(TTX, "<&0") || die "Failed to dup STDIN: $!\n";
         $VbiCaptureTime = time;
      } else {
         open(TTX, "<$opt_infile") || die "Failed to open $opt_infile: $!\n";
         $VbiCaptureTime = (stat($opt_infile))[9];
      }
      binmode(TTX);
   } else {
      # capture input data from a VBI device
      $VbiCaptureTime = time;
      $cap = DeviceOpen();
      @CapPkgs = ();
      if (defined($opt_outfile)) {
         close STDOUT;
         open(STDOUT, ">$opt_outfile") || die "Failed to create $opt_outfile: $!\n";
      }
   }

   $intv = 0;
   FRAMELOOP:
   while (1) {
      if (defined $opt_infile) {
         my $line_off = 0;
         my $rstat;
         my $buf;
         while (($rstat = sysread(TTX, $buf, 46 - $line_off, $line_off)) > 0) {
            $line_off += $rstat;
            last if $line_off >= 46;
         }
         last FRAMELOOP if $rstat <= 0;
         ($page, $ctl1, $ctl2, $pkg, $data) = unpack("SSCCa40", $buf);
         if (($pkg == 30) || ($pkg == 32)) {
            # extract CNI value
            $data = (unpack("Sx18a20", $data))[0];
         }
      } else {
         while ($#CapPkgs < 0) {
            if (($opt_duration > 0) && ((time - $VbiCaptureTime) > $opt_duration)) {
               # capture duration limit reached -> terminate loop
               last FRAMELOOP;
            }
            push @CapPkgs, DeviceRead($cap, \$last_pg);
         }
         ($page, $ctl1, $ctl2, $pkg, $data) = splice(@CapPkgs, 0, 5, ());
      }

      $page += 0x800 if $page < 0x100;
      $mag = ($page >> 8) & 7;
      $sub = $ctl1 & 0x3f7f;
      #printf "%03X.%04X, %2d %.40s\n", $page, $sub, $pkg, $data;

      if ($mag_serial && ($mag != $cur_mag) && ($pkg <= 29)) {
         $CurPage[$cur_mag] = -1;
      }

      if ($pkg == 0) {
         # erase page control bit (C4) - ignored, some channels abuse this
         #if ($ctl1 & 0x80) {
         #   VbiPageErase($page);
         #}
         # inhibit display control bit (C10)
         #if ($ctl2 & 0x08) {
         #   $CurPage[$mag] = -1;
         #   next;
         #}
         if ($ctl2 & 0x01) {
            $data = " " x 40;
         }
         if ( (($page & 0x0F) > 9) || ((($page >> 4) & 0x0f) > 9) ) {
            $page = $CurPage[$mag] = -1;
            next;
         }
         # magazine serial mode (C11: 1=serial 0=parallel)
         $mag_serial = (($ctl2 & 0x10) != 0);
         if (!defined($PgCnt{$page})) {
            # new page
            $Pkg{$page|($sub<<12)} = [];
            $PgCnt{$page} = 1;
            $PgSub{$page} = $sub;
            # store G0 char set (bits must be reversed: C12,C13,C14)
            $PgLang{$page} = (($ctl2 >> 7) & 1) | (($ctl2 >> 5) & 2) | (($ctl2 >> 3) & 4);
         } elsif (($page != $CurPage[$mag]) || ($sub != $CurSub[$mag])) {
            # repeat page (and not just repeated page header, e.g. as filler packet)
            # TODO count sub-pages separately
            $PgCnt{$page} += 1;
            $PgSub{$page} = $sub if $sub >= $PgSub{$page};
         }

         # check if every page (NOT sub-page) was received N times in average
         # (only do the check every 50th page to save CPU)
         if ($opt_duration == 0) {
            $intv += 1;
            if ($intv >= 50) {
               my $page_cnt = 0;
               my $page_rep = 0;
               foreach (keys %PgCnt) {
                  $page_cnt += 1;
                  $page_rep += $PgCnt{$_};
               }
               last FRAMELOOP if ($page_cnt > 0) && (($page_rep / $page_cnt) >= 10);
               $intv = 0;
            }
         }

         $CurPage[$mag] = $page;
         $CurSub[$mag] = $sub;
         $CurPkg[$mag] = 0;
         $cur_mag = $mag;
         $Pkg{$page|($sub<<12)}->[0] = $data;

      } elsif ($pkg <= 27) {
         $page = $CurPage[$mag];
         $sub = $CurSub[$mag];
         if ($page != -1) {
            # page body packets are supposed to be in order
            if ($pkg < $CurPkg[$mag]) {
               $page = $CurPage[$mag] = -1;
            } else {
               $CurPkg[$mag] = $pkg if $pkg < 26;
               $Pkg{$page|($sub<<12)}->[$pkg] = $data;
            }
         }
      } elsif (($pkg == 30) || ($pkg == 32)) {
         $PkgCni{$data} += 1;
      }
   }
   if (defined $opt_infile) {
      close(TTX);
   }
}

# Erase the page with the given number from memory
# used to handle the "erase" control bit in the TTX header
sub VbiPageErase {
   my ($page) = @_;
   my $sub;
   my $pkg;

   if (defined($PgSub{$page})) {
      for ($sub = 0; $sub <= $PgSub{$page}; $sub++) {
         delete $Pkg{$page|($sub<<12)};
      }
      delete $PgCnt{$page};
      delete $PgSub{$page};
   }
}

# Check if the given sub-page of the given page exists
sub TtxSubPageDefined {
   my ($page, $sub) = @_;
   my $handle;

   $handle = $page | ($sub << 12);
   return defined $Pkg{$handle}->[0];
}

# ------------------------------------------------------------------------------
# Dump all loaded teletext pages as plain text
# - teletext control characters and mosaic is replaced by space
# - used for -dump option, intended for debugging only
#
sub DumpTextPages {
   my $page;

   if (defined($opt_outfile)) {
      close STDOUT;
      open(STDOUT, ">$opt_outfile") || die "Failed to create $opt_outfile: $!\n";
   }

   foreach $page (sort {$a<=>$b} keys(%PgCnt)) {
      my $line;
      my ($sub, $sub1, $sub2);
      my $pgtext;
      my $handle;

      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         $handle = $page | ($sub << 12);
         if (defined($Pkg{$handle}->[0])) {
            printf "PAGE %03X.%04X\n", $page, $sub;

            PageToLatin($page, $sub);
            $pgtext = $PageText{$handle};

            for ($line = 1; $line <= 23; $line++) {
               print $pgtext->[$line] . "\n";
            }
            print "\n";
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Dump all loaded teletext data as Perl script
# - the generated script can be loaded with the -verify option
#
sub DumpRawTeletext {
   my $page;
   my $cni;

   if (defined($opt_outfile)) {
      close STDOUT;
      open(STDOUT, ">$opt_outfile") || die "Failed to create $opt_outfile: $!\n";
   }

   print "#!/usr/bin/perl -w\n".
         "\$VbiCaptureTime = $VbiCaptureTime;\n";

   foreach $cni (sort { $PkgCni{$b} <=> $PkgCni{$a} } keys %PkgCni) {
      printf("\$PkgCni{0x%X} = %d;\n", $cni, $PkgCni{$cni});
   }

   foreach $page (sort {$a<=>$b} keys(%PgCnt)) {
      my $pkg;
      my ($sub, $sub1, $sub2);
      my $handle;

      next unless (($page >= $opt_tv_start) && ($page <= $opt_tv_end));

      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         $handle = $page | ($sub << 12);

         # skip missing sub-pages
         if (defined($Pkg{$handle}->[0])) {
            printf("\$PgCnt{0x%03X} = %d;\n", $page, $PgCnt{$page});
            printf("\$PgSub{0x%03X} = %d;\n", $page, $PgSub{$page});
            printf("\$PgLang{0x%03X} = %d;\n", $page, $PgLang{$page});

            printf("\$Pkg{0x%03X|(0x%04X<<12)} =\n\[\n", $page, $sub);

            for ($pkg = 0; $pkg <= 23; $pkg++) {
               $_ = $Pkg{$handle}->[$pkg];
               if (defined($_)) {
                  # quote Perl special characters
                  s#([\"\$\@\%\\])#\\$1#g;
                  # quote binary characters
                  s#([\x00-\x1F\x7F])#"\\x".sprintf("%02X",ord($1))#eg;
                  print "  \"$_\",\n";
               } else {
                  print "  undef,\n";
               }
            }
            print "\];\n";
         }
      }
   }
   # return TRUE to allow to "require" the file
   print "1;\n";
}

# ------------------------------------------------------------------------------
# Import a data file generated by DumpRawTeletext
#
sub ImportRawDump {
   my $file = "";

   if ($opt_infile eq "") {
      open(DUMP, "<&0") || die "Failed to dup STDIN: $!\n";
   } else {
      open(DUMP, "<$opt_infile") || die "Failed to open $opt_infile: $!\n";
   }

   while ($_ = <DUMP>) {
      $file .= $_;
   }
   close(DUMP);

   eval $file || die "syntax error in $opt_infile: $@\n";
}

# ------------------------------------------------------------------------------
# Conversion of teletext G0 charset into ISO-8859-1
#
my @NationalOptionsMatrix =
(
   #0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x00
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x10
   -1, -1, -1,  0,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x20
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x30
    2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x40
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  3,  4,  5,  6,  7,  # 0x50
    8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x60
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9, 10, 11, 12, -1   # 0x70
);

my @NatOptMaps =
(
    # for latin-1 font
    # English (100%)
    '£$@«½»¬#­¼¦¾÷',
    # German (100%)
    '#$§ÄÖÜ^_°äöüß',
    # Swedish/Finnish/Hungarian (100%)
    '#¤ÉÄÖÅÜ_éäöåü',
    # Italian (100%)
    '£$é°ç»¬#ùàòèì',
    # French (100%)
    'éïàëêùî#èâôûç',
    # Portuguese/Spanish (100%)
    'ç$¡áéíóú¿üñèà',
    # Czech/Slovak (60%)
    '#uctzýíréáeús',
    # reserved (English mapping)
    '£$@«½»¬#­¼¦¾÷',
    # Polish (6%: all but '#' missing)
    '#naZSLcoezslz',
    # German (100%)
    '#$§ÄÖÜ^_°äöüß',
    # Swedish/Finnish/Hungarian (100%)
    '#¤ÉÄÖÅÜ_éäöåü',
    # Italian (100%)
    '$é°ç»¬#ùàòèì',
    # French (100%)
    'ïàëêùî#èâôûç',
    # reserved (English mapping)
    '$@«½»¬#­¼¦¾÷',
    # Czech/Slovak 
    'uctzýíréáeús',
    # reserved (English mapping)
    '$@«½»¬#­¼¦¾÷',
    # English 
    '$@«½»¬#­¼¦¾÷',
    # German
    '$§ÄÖÜ^_°äöüß',
    # Swedish/Finnish/Hungarian (100%)
    '¤ÉÄÖÅÜ_éäöåü',
    # Italian (100%'
    '$é°ç»¬#ùàòèì',
    # French (100%)
    'ïàëêùî#èâôûç',
    # Portuguese/Spanish (100%)
    '$¡áéíóú¿üñèà',
    # Turkish (50%: 7 missing)
    'gISÖÇÜGisöçü',
    # reserved (English mapping)
    '$@«½»¬#­¼¦¾÷'
);

my @DelSpcAttr = (
    0,0,0,0,0,0,0,0,    # 0-7: alpha color
    1,1,1,1,            # 8-B: flash, box
    0,0,0,0,            # C-F: size
    1,1,1,1,1,1,1,1,    # 10-17: mosaic color
    0,                  # 18:  conceal
    1,1,                # 19-1A: continguous mosaic
    0,0,0,              # 1B-1D: charset, bg color
    1,1                 # 1E-1F: hold mosaic
);

# TODO: evaluate packet 26 and 28
sub TtxToLatin1 {
   my($line, $g0_set) = @_;
   my $idx;
   my $val;

   my $is_g1 = 0;
   for ($idx = 0; $idx < length($line); $idx++) {
      $val = ord(substr($line, $idx, 1));

      # alpha color code
      if ($val <= 0x07) {
         $is_g1 = 0;
         #substr($line, $idx, 1) = ' ';

      # mosaic color code
      } elsif (($val >= 0x10) && ($val <= 0x17)) {
         $is_g1 = 1;
         substr($line, $idx, 1) = ' ';

      # other control character or mosaic character
      } elsif ($val < 0x20) {
         if ($DelSpcAttr[$val]) {
            substr($line, $idx, 1) = ' ';
         }

      # mosaic charset
      } elsif ($is_g1) {
         substr($line, $idx, 1) = ' ';

      } elsif ( ($val < 0x80) && ($NationalOptionsMatrix[$val] >= 0) ) {
         if ($g0_set <= $#NatOptMaps) {
            $val = substr($NatOptMaps[$g0_set], $NationalOptionsMatrix[$val], 1);
         } else {
            $val = ' ';
         }
         substr($line, $idx, 1) = $val;
      }
   }
   return $line;
}

sub PageToLatin {
   my ($page, $sub) = @_;
   my $handle = $page | ($sub << 12);

   if (!defined($PageText{$handle})) {
      my @PgText = ("");
      my @PgCtrl = ("");
      my $lang = $PgLang{$page};
      my $pkgs = $Pkg{$handle};
      my $idx;

      for ($idx = 1; $idx <= 23; $idx++) {
         if (defined($pkgs->[$idx])) {
            $_ = TtxToLatin1($pkgs->[$idx], $lang);
            push @PgCtrl, $_;
            s#[\x00-\x1F\x7F]# #g;
            push @PgText, $_;
         } else {
            $_ = " " x 40;
            push @PgCtrl, $_;
            push @PgText, $_;
         }
      }
      $PageCtrl{$handle} = \@PgCtrl;
      $PageText{$handle} = \@PgText;
   }
}

# ------------------------------------------------------------------------------
# Tables to support parsing of dates in various languages
# - the hash tables map each word to an array with 3 elements:
#   0: bitfield of language (language indices as in teletext header)
#      use a bitfield because some words are identical in different languages
#   1: value (i.e. month or weekday index)
#   2: bitfield abbreviation: 1:non-abbr., 2:abbr., 3:no change by abbr.
#      use a bitfield because some short words are not abbreviated (e.g. "may")

# map month names to month indices 1..12
my %MonthNames =
(
   # English (minus april, august, september)
   "january" => [(1<<0),1,1], "february" => [(1<<0),2,1], "march" => [(1<<0),3,1],
   "may" => [(1<<0),5,3], "june" => [(1<<0),6,1], "july" => [(1<<0),7,1],
   "october" => [(1<<0),10,1], "december" => [(1<<0),12,1],
   "mar" => [(1<<0),3,2], "oct" => [(1<<0),10,2], "dec" => [(1<<0),12,1],
   # German
   "januar" => [(1<<1),1,1], "februar" => [(1<<1),2,1], "märz" => [(1<<1),3,1],
   "april" => [(1<<1)|(1<<0),4,1], "mai" => [(1<<4)|(1<<1),5,3], "juni" => [(1<<1),6,1],
   "juli" => [(1<<1),7,1], "august" => [(1<<1)|(1<<0),8,1], "september" => [(1<<1)|(1<<0),9,1],
   "oktober" => [(1<<1),10,1], "november" => [(1<<1)|(1<<0),11,1], "dezember" => [(1<<1),12,1],
   "jan" => [(1<<1)|(1<<0),1,2], "feb" => [(1<<1)|(1<<0),2,2],
   "mär" => [(1<<1),3,2], "mar" => [(1<<1),3,2],
   "apr" => [(1<<1)|(1<<0),4,2], "jun" => [(1<<1)|(1<<0),6,2], "jul" => [(1<<1)|(1<<0),7,2],
   "aug" => [(1<<1)|(1<<0),8,2], "sep" => [(1<<1)|(1<<0),9,2], "okt" => [(1<<1),10,2],
   "nov" => [(1<<1)|(1<<0),11,2], "dez" => [(1<<1),12,2],
   # French
   "janvier" => [(1<<4),1,1], "février" => [(1<<4),2,1], "mars" => [(1<<4),3,1],
   "avril" => [(1<<4),4,1],
   "juin" => [(1<<4),6,1], "juillet" => [(1<<4),7,1], "août" => [(1<<4),8,1],
   "septembre" => [(1<<4),9,1], "octobre" => [(1<<4),10,1],
   "novembre" => [(1<<4),11,1], "décembre" => [(1<<4),12,1]
);

# map week day names to indices 0..6 (0=sunday)
my %WDayNames =
(
   # English
   "sat" => [(1<<0),6,2], "sun" => [(1<<0),0,2], "mon" => [(1<<0),1,2], "tue" => [(1<<0),2,2],
   "wed" => [(1<<0),3,2], "thu" => [(1<<0),4,2], "fri" => [(1<<0),5,2],
   "saturday" => [(1<<0),6,1], "sunday" => [(1<<0),0,1], "monday" => [(1<<0),1,1], "tuesday" => [(1<<0),2,1],
   "wednesday" => [(1<<0),3,1], "thursday" => [(1<<0),4,1], "friday" => [(1<<0),5,1],
   # German
   "sa" => [(1<<1),6,2], "so" => [(1<<1),0,2], "mo" => [(1<<1),1,2], "di" => [(1<<1),2,2],
   "mi" => [(1<<1),3,2], "do" => [(1<<1),4,2], "fr" => [(1<<1),5,2],
   "sam" => [(1<<1),6,2], "son" => [(1<<1),0,2], "mon" => [(1<<1),1,2], "die" => [(1<<1),2,2],
   "mit" => [(1<<1),3,2], "don" => [(1<<1),4,2], "fre" => [(1<<1),5,2],
   "samstag" => [(1<<1),6,1], "sonnabend" => [(1<<1),6,1], "sonntag" => [(1<<1),0,1],
   "montag" => [(1<<1),1,1], "dienstag" => [(1<<1),2,1], "mittwoch" => [(1<<1),3,1],
   "donnerstag" => [(1<<1),4,1], "freitag" => [(1<<1),5,1],
   # French
   "samedi" => [(1<<4),6,1], "dimanche" => [(1<<4),0,1], "lundi" => [(1<<4),1,1], "mardi" => [(1<<4),2,1],
   "mercredi" => [(1<<4),3,1], "jeudi" => [(1<<4),4,1], "vendredi" => [(1<<4),5,1]
);

# map today, tomorrow etc. to day offsets 0,1,...
my %RelDateNames =
(
   "today" => [(1<<0),0,1], "tomorrow" => [(1<<0),1,1],
   "heute" => [(1<<1),0,1], "morgen" => [(1<<1),1,1], "übermorgen" => [(1<<1),2,1],
   "aujourd'hui" => [(1<<4),0,1], "demain" => [(1<<4),1,1], "aprés-demain" => [(1<<4),2,1]
);

# return a reg.exp. pattern which matches all names of a given language
sub GetDateNameRegExp {
   my ($href, $lang, $abbrv) = @_;
   my $def;

   $lang = 1 << $lang;

   my @Pat = ();
   foreach (keys(%{$href})) {
      $def = $href->{$_};
      if (($def->[0] & $lang) != 0) {
         if (!defined($abbrv) || (($def->[2] & $abbrv) != 0)) {
            push @Pat, $_;
         }
      }
   }
   return join("|", sort(@Pat));
}

# ------------------------------------------------------------------------------
#  Determine layout of programme overview pages
#  (to allow stricter parsing)
#  - TODO: detect color used to mark features (e.g. yellow on ARD) (WDR: ttx ref same color as title)
#  - TODO: detect color used to distinguish title from subtitle/category (WDR,Tele5)
#
sub DetectOvFormat {
   my ($page, $pkg);
   my ($sub, $sub1, $sub2);
   my %FmtStat = ();
   my %SubtStat = ();

   # look at the first 5 pages (default start at page 301)
   my @Pages = grep {($_ >= $opt_tv_start)&&($_ <= $opt_tv_end)} keys(%PgCnt);
   @Pages = sort {$a<=>$b} @Pages;
   splice(@Pages, 5) if $#Pages >= 5;

   foreach $page (@Pages) {
      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         PageToLatin($page, $sub);

         DetectOvFormatParse(\%FmtStat, \%SubtStat, $page, $sub);
      }
   }

   my @FmtSort = sort { $FmtStat{$b} <=> $FmtStat{$a} } keys %FmtStat;
   my $fmt;
   if (defined($FmtSort[0])) {
      $fmt = $FmtSort[0];
      my @SubtSort = grep(substr($_, length($_)-length($fmt)) eq $fmt, keys(%SubtStat));
      @SubtSort = sort { $SubtStat{$b} <=> $SubtStat{$a} } @SubtSort;
      if ($#SubtSort >= 0) {
         $fmt .= ",". (split(/,/, $SubtSort[0]))[0];
      } else {
         # fall back to title offset
         $fmt .= ",". (split(/,/, $fmt))[0];
      }
      print "auto-detected overview format: $fmt\n" if $opt_debug;
   } else {
      $fmt = undef;
      print "auto-detected overview failed, pages ".join(",",@Pages)."\n" if $opt_debug;
   }
   return $fmt;
}

# ! 20.15  Eine Chance für die Liebe UT         ARD
#           Spielfilm, D 2006 ........ 344
#                     ARD-Themenwoche Krebs         
#
# 19.35  1925  WISO ................. 317       ZDF
# 20.15        Zwei gegen Zwei           
#              Fernsehfilm D/2005 UT  318
#
# 10:00 WORLD NEWS                              CNNi (week overview)
# News bulletins on the hour (...)
# 10:30 WORLD SPORT                      
#
sub DetectOvFormatParse {
   my ($fmt, $subt, $page, $sub) = @_;
   my ($off, $time_off, $vps_off, $title_off, $subt_off);
   my $line;
   my $handle;
   my $pgtext = $PageText{$page | ($sub << 12)};

   for ($line = 5; $line <= 21; $line++) {
      $_ = $pgtext->[$line];
      # look for a line containing a start time
      # TODO allow start-stop times "10:00-11:00"?
      if ( (m#^(( *| *\! +)(\d\d)[\.\:](\d\d) +)#) &&
           ($3 < 24) && ($4 < 60) ) {

         $off = length $1;
         $time_off = length $2;

         # TODO VPS must be magenta or concealed
         if (substr($_, $off) =~ m#^(\d{4} +)#) {
            $vps_off = $off;
            $off += length $1;
            $title_off = $off;
         } else {
            $vps_off = -1;
            $title_off = $off;
         }
         $handle = "$time_off,$vps_off,$title_off";
         if (defined ($fmt->{$handle})) {
            $fmt->{$handle} += 1;
         } else {
            $fmt->{$handle} = 1;
         }

         if ( ($vps_off == -1) ?
              ($pgtext->[$line + 1] =~ m#^( *| *\d{4} +)[[:alpha:]]#) :
              ($pgtext->[$line + 1] =~ m#^( *)[[:alpha:]]#) ) {

            $subt_off = length($1);
            $handle = "$subt_off,$time_off,$vps_off,$title_off";
            if (defined ($subt->{$handle})) {
               $subt->{$handle} += 1;
            } else {
               $subt->{$handle} = 1;
            }
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Parse date in an overview page
# - similar to dates on description pages
#
sub ParseOvHeaderDate {
   my ($page, $sub, $pgdate) = @_;
   my $reldate;
   my ($day_of_month, $month, $year);

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $lang = $PgLang{$page};
   my $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
   my $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
   my $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);
   my $relday_match = GetDateNameRegExp(\%RelDateNames, $lang, undef);
   my $prio = -1;

   PAGE_LOOP:
   for (my $line = 1; $line < $pgdate->{head_end}; $line++) {
      $_ = $pgtext->[$line];

      {
         # [Mo.]13.04.[2006]
         # So,06.01.
         if (m#(^| |($wday_abbrv_match)(\.|\.,|,) ?)(\d{1,2})\.(\d{1,2})\.(\d{2}|\d{4})?([ ,:]|$)#i ||
             m#(^| |($wday_match)(, ?| ))(\d{1,2})\.(\d{1,2})\.(\d{2}|\d{4})?([ ,:]|$)#i) {
            my @NewDate = CheckDate($4, $5, $6, undef, undef);
            if (@NewDate) {
               ($day_of_month, $month, $year) = @NewDate;
               $prio = 3;
            }
         }
         # 13.April [2006]
         if (m#(^| )(\d{1,2})\. ?($mname_match)( (\d{2}|\d{4}))?([ ,:]|$)#i) {
            my @NewDate = CheckDate($2, undef, $5, undef, $3);
            if (@NewDate) {
               ($day_of_month, $month, $year) = @NewDate;
               $prio = 3;
            }
         }
         # Sunday 22 April (i.e. no dot after month)
         if ( m#(^| )($wday_match)(, ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|;|$)#i ||
              m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|;|$)#i) {
            my @NewDate = CheckDate($4, undef, $7, $2, $5);
            if (@NewDate) {
               ($day_of_month, $month, $year) = @NewDate;
               $prio = 3;
            }
         }
         next PAGE_LOOP if $prio >= 3;
         # "Do. 21-22 Uhr" (e.g. on VIVA)  --  TODO internationalize "Uhr"
         # "Do  21:00-22:00" (e.g. Tele-5)
         if (m#(^| )(($wday_abbrv_match)\.?|($wday_match)) *((\d{1,2}(\-| \- )\d{1,2}( +Uhr| ?h|   ))|(\d{1,2}[\.\:]\d{2}(\-| \- )(\d{1,2}[\.\:]\d{2})?))( |$)#i) {
            my $wday_name = $2;
            $wday_name = $3 if !defined($2);
            my $off = GetWeekDayOffset($wday_name);
            $reldate = $off if $off >= 0;
            $prio = 2;
         }
         next PAGE_LOOP if $prio >= 2;
         # monday, tuesday, ... (non-abbreviated only)
         if (m#(^| )($wday_match)([ ,:]|$)#i) {
            my $off = GetWeekDayOffset($2);
            $reldate = $off if $off >= 0;
            $prio = 1;
         }
         next PAGE_LOOP if $prio >= 1;
         # today, tomorrow, ...
         if (m#(^| )($relday_match)([ ,:]|$)#i) {
            my $rel_name = lc($2);
            $reldate = $RelDateNames{$rel_name}->[1] if defined($RelDateNames{$rel_name});
            $prio = 0;
         }
      }
   }
   if (!defined($day_of_month) && defined($reldate)) {
      ($day_of_month, $month, $year) = (localtime($VbiCaptureTime + $reldate*24*60*60))[3,4,5];
      $month += 1;
      $year += 1900;
   } elsif (defined($year) && ($year < 100)) {
      my $cur_year = (localtime($VbiCaptureTime))[5] + 1900;
      $year = ($cur_year - $cur_year%100);
   }

   # return results via the struct
   $pgdate->{day_of_month} = $day_of_month;
   $pgdate->{month} = $month;
   $pgdate->{year} = $year;
   $pgdate->{date_off} = 0;
}

# ------------------------------------------------------------------------------
# Chop footer
# - footers usually contain advertisments (or more precisely: references
#   to teletext pages with ads) and can simply be discarded

#   Schule. Dazu Arbeitslosigkeit von    
#   bis zu 30 Prozent. Wir berichten ü-  
#   ber Menschen in Ostdeutschland.  318>
#    Ab in die Sonne!................500 

#   Teilnahmebedingungen: Seite 881      
#     Türkei-Urlaub jetzt buchen! ...504 


sub ParseFooter {
   my ($page, $sub, $head) = @_;
   my $foot;

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $pgctrl = $PageCtrl{$page | ($sub << 12)};

   for ($foot = $#{$pgtext} ; $foot > $head; $foot--) {
      # note missing lines are treated as empty lines
      $_ = $pgtext->[$foot];

      # stop at lines which look like separators
      if (m#^ *\-{10,} *$#) {
         $foot -= 1;
         last;
      }
      if (!m#\S#) {
         if ($pgctrl->[$foot - 1] !~ m#[\x0D\x0F][^\x0C]*[^ ]#) {
            $foot -= 1;
            last;
         }
         # ignore this empty line since there's double-height chars above
         next;
      }
      # check for a teletext reference
      # TODO internationalize
      if ( m#^ *(seite |page |\<+ *)?[1-8][0-9][0-9][^\d]#i ||
           m#[^d][1-8][0-9][0-9]([\.\!\?\:\,]|\>+)? *$# ) {
           #|| (($sub != 0) && m#(^ *<|> *$)#) ) {
      } else {
         last;
      }
   }
   # plausibility check (i.e. don't discard the entire page)
   if ($foot < $#{$pgtext} - 5) {
      $foot = $#{$pgtext};
   }
   return $foot;
}

# Try to identify footer lines by a different background color
# - bg color is changed by first changing the fg color (codes 0x0-0x7),
#   then switching copying fg over bg color (code 0x1D)
#
sub ParseFooterByColor {
   my ($page, $sub) = @_;
   my $handle;
   my $line;
   my @ColCount = (0, 0, 0, 0, 0, 0, 0, 0);
   my @LineCol;

   # check which color is used for the main part of the page
   # - ignore top-most header and one footer line
   # - ignore missing lines (although they would display black (except for
   #   double-height) but we don't know if they're intentionally missing)
   $handle = $page | ($sub << 12);
   for ($line = 4; $line <= 22; $line++) {
      if (defined($Pkg{$handle}->[$line])) {
         # get first non-blank char; skip non-color control chars
         if ($Pkg{$handle}->[$line] =~ m#^[ \x00-\x1F]*([\x00-\x07\x10-\x17])\x1D#) {
            my $c = ord($1);
            if ($c <= 0x07) {
               $ColCount[$c] += 1;
            }
            $LineCol[$line] = $c;
            # else: ignore mosaic
         } else {
            # background color unchanged
            $ColCount[0] += 1;
            $LineCol[$line] = 0;
         }
      }
   }

   # search most used color
   my $max_idx = 0;
   my $col_idx;
   for ($col_idx = 0; $col_idx < 8; $col_idx++) {
      if ($ColCount[$col_idx] > $ColCount[$max_idx]) {
         $max_idx = $col_idx;
      }
   }

   # count how many consecutive lines have this color
   # FIXME allow also use of several different colors in the middle and different ones for footer
   my $max_count = 0;
   my $count = 0;
   for ($line = 4; $line <= 23; $line++) {
      if (!defined($LineCol[$line]) || ($LineCol[$line] == $max_idx)) {
         $count += 1;
         $max_count = $count if $count > $max_count;
      } else {
         $count = 0;
      }
   }

   # TODO: merge with other footer function: require TTX ref in every skipped segment with the same bg color

   if ($max_count >= 8) {
      # skip footer until normal color is reached
      for ($line = 23; $line >= 0; $line--) {
         if (defined($LineCol[$line]) && ($LineCol[$line] == $max_idx)) {
            last;
         }
      }
   } else {
      # not reliable - too many color changes, don't skip any footer lines
      $line = 23;
   }
   return $line;
}

# ------------------------------------------------------------------------------
# Remove garbage from line end in overview title lines
# - KiKa sometimes uses the complete page for the overview and includes
#   the footer text at the right side on a line used for the overview,
#   separated by using a different background color
#   " 09.45 / Dragon (7)        Weiter   >>> " (line #23)
# - note the cases with no text before the page ref is handled b the
#   regular footer detection
#
sub RemoveTrailingPageFooter {
   my ($foo, $title_off) = @_;

   # look for a page reference or ">>" at line end
   if ($_[0] =~ m#(.*[^[:alnum:]])([1-8][0-9][0-9]|\>{1,4})[^\x1D\x21-\xFF]*$#) {
      my $ref_off = length($1);
      # check if the background color is changed
      if (substr($_[0], 0, $ref_off) =~ m#(.*)\x1D[^\x1D]+$#) {
         $ref_off = length($1);
         # determine the background color of the reference (i.e. last used FG color before 1D)
         my $ref_col = 7;
         if (substr($_[0], 0, $ref_off) =~ m#(.*)([\x00-\x07\x10-\x17])[^\x00-\x07\x10-\x17\x1D]*$#) {
            $ref_col = ord($2);
         }
         #print "       REF OFF:$ref_off COL:$ref_col\n";
         # determine the background before the reference
         my $matched = 0;
         my $txt_off = $ref_off;
         if (substr($_[0], 0, $ref_off) =~ m#(.*)\x1D[^\x1D]+$#) {
            my $tmp_off = length($1);
            if (substr($_[0], 0, $tmp_off) =~ m#(.*)([\x00-\x07\x10-\x17])[^\x00-\x07\x10-\x17\x1D]*$#) {
               my $txt_col = ord($2);
               #print "       TXTCOL:$txt_col\n";
               # check if there's any text with this color
               if (substr($_[0], $tmp_off, $ref_off) =~ m#[\x21-\xff]#) {
                  $matched = ($txt_col != $ref_col);
                  $txt_off = $tmp_off;
               }
            }
         }
         # check for text at the default BG color (unless ref. has default too)
         if ( !$matched && ($ref_col != 7) &&
              (substr($_[0], 0, $txt_off) =~ m#[\x21-\xff]#) ) {
            $matched = 1;
            #print "       DEFAULT TXTCOL:7\n";
         }
         if ($matched) {
            print "OV removing trailing page ref\n" if $opt_debug;
            substr($_[0], $ref_off) = " " x (length($_[0]) - $ref_off);
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Retrieve programme entries from an overview page
# - the layout has already been determined in advance, i.e. we assume that we
#   have a tables with strict columns for times and titles; rows that don't
#   have the expected format are skipped (normally only header & footer)
#
sub ParseOvList {
   my ($page, $sub, $foot_line, $pgfmt, $pgdate) = @_;
   my ($hour, $min, $title, $is_tip);
   my $line;
   my $new_title;
   my $in_title = 0;
   my $slot = {};
   my $vps_data = {};
   my @Slots = ();

   my ($time_off, $vps_off, $title_off, $subt_off) = split(",", $pgfmt);
   my $pgctrl = $PageCtrl{$page | ($sub << 12)};

   for ($line = 1; $line <= 23; $line++) {
      # note: use text including control-characters, because the next 2 steps require these
      $_ = $pgctrl->[$line];

      if ($line >= 23) {
         RemoveTrailingPageFooter($_);
      }
      # extract and remove VPS labels
      # (labels not asigned here since we don't know yet if a new title starts in this line)
      $vps_data->{new_vps_time} = 0;
      if (m#[\x05\x18]+ *[^\x00-\x20]#) {
         ParseVpsLabel($_, $vps_data, 0);
      }
      # remove remaining control characters
      (my $text = $_) =~ s#[\x00-\x1F\x7F]# #g;

      $new_title = 0;
      if ($vps_off >= 0) {
         # TODO: Phoenix wraps titles into 2nd line, appends subtitle with "-"
         # m#^ {0,2}(\d\d)[\.\:](\d\d)( {1,3}(\d{4}))? +#
         if (substr($text, 0, $time_off) =~ m#^ *([\*\!] +)?$#) {
            $is_tip = $1;
            if (substr($text, $time_off, $vps_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) {
               $hour = $1;
               $min = $2;
               # VPS time has already been extractied above
               if (substr($text, $vps_off, $title_off - $vps_off) =~ m#^ +$#) {
                  $new_title = 1;
                  $title = substr($_, $title_off);
               }
            }
         }
      } else {
         # m#^( {0,5}| {0,3}\! {1,3})(\d\d)[\.\:](\d\d) +#
         if (substr($text, 0, $time_off) =~ m#^ *([\*\!] +)?$#) {
            $is_tip = $1;
            if (substr($text, $time_off, $title_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) {
               $hour = $1;
               $min = $2;
               $new_title = 1;
               $title = substr($_, $title_off);
            }
         }
      }

      if ($new_title) {
         # remember end of page header for date parser
         $pgdate->{head_end} = $line unless defined $pgdate->{head_end};

         # push previous title
         push(@Slots, $slot) if defined $slot->{title};
         $slot = {};

         $slot->{hour} = $hour;
         $slot->{min} = $min;
         $slot->{is_tip} = $is_tip if defined $is_tip;
         $slot->{vps_time} = $vps_data->{vps_time} if $vps_data->{new_vps_time};
         $slot->{vps_cni} = $vps_data->{vps_cni};
         if ($vps_data->{new_vps_date}) {
            $slot->{vps_date} = $vps_data->{vps_date};
            $vps_data->{new_vps_date} = 0;
         }

         $title = ParseTrailingFeat($title, $slot);
         print "OV TITLE: \"$title\", $slot->{hour}:$slot->{min}\n" if $opt_debug;

         # kika special: subtitle appended to title
         if ($title =~ m#(.*\(\d+( *[\&\-] *\d+)?\))\/ *(\S.*)#) {
            $title = $1;
            $slot->{subtitle} = $3;
            $slot->{subtitle} =~ s# +$# #;
         }
         $slot->{title} = $title;
         $in_title = 1;
         #print "ADD  $slot->{day_of_month}.$slot->{month} $slot->{hour}.$slot->{min} $slot->{title}\n";

      } elsif ($in_title) {

         # stop appending subtitles before page footer ads
         last if $line > $foot_line;

         $slot->{vps_time} = $vps_data->{vps_time} if $vps_data->{new_vps_time};

         # check if last line specifies and end time
         # (usually last line of page)
         # TODO internationalize "bis", "Uhr"
         if ( $text =~ m#^ *(\(?bis |ab |\- ?)(\d{1,2})[\.\:](\d{2})( Uhr| ?h)( |\)|$)# ||   # ARD,SWR,BR-alpha
              $text =~ m#^( *)(\d\d)[\.\:](\d\d) (oo)? *$# ) {                               # arte, RTL
            $slot->{end_hour} = $2;
            $slot->{end_min} = $3;
            $new_title = 1;
            print "Overview end time: $slot->{end_hour}:$slot->{end_min}\n" if $opt_debug;

         # check if we're still in a continuation of the previous title
         # time column must be empty (possible VPS labels were already removed above)
         } else {
            if ( (substr($text, 0, $subt_off) =~ m#[^ \x00-\x07\x10-\x17]#) ||
                 (substr($text, $subt_off) =~ m#^[ \x00-\x07\x10-\x17]*$#) ) {
               $new_title = 1;
            }
         }
         if ($new_title == 0) {
            $_ = ParseTrailingFeat($_, $slot);
            # combine words separated by line end with "-"
            if ( !defined($slot->{subtitle}) &&
                 ($slot->{title} =~ m#\S\-$#) && m#^[[:lower:]]#) {
               $slot->{title} =~ s#\-$##;
               $slot->{title} .= $_;
            } else {
               if ( defined($slot->{subtitle}) &&
                    ($slot->{subtitle} =~ m#\S\-$#) && m#^[[:lower:]]#) {
                  $slot->{subtitle} =~ s#\-$##;
                  $slot->{subtitle} .= $_;
               } elsif (defined($slot->{subtitle})) {
                  $slot->{subtitle} .= " " . $_;
               } else {
                  $slot->{subtitle} = $_;
               }
            }
            if ($vps_data->{new_vps_date}) {
               $slot->{vps_date} = $vps_data->{vps_date};
               $vps_data->{new_vps_date} = 0;
            }
         } else {
            push(@Slots, $slot) if defined $slot->{title};
            $slot = {};
            $in_title = 0;
         }
      }
   }
   push(@Slots, $slot) if defined $slot->{title};

   return @Slots;
}

# ------------------------------------------------------------------------------
# Parse and remove VPS indicators
# - format as defined in ETS 300 231, ch. 7.3.1.3
# - for performance reasons the caller should check if there's a magenta
#   or conceal character in the line before calling this function
#
sub ParseVpsLabel {
   my ($foo, $vps_data, $is_desc) = @_;

   # time
   # discard any concealed/magenta labels which follow
   if ($_[0] =~ m#^(.*)([\x05\x18]+(VPS[\x05\x18 ]+)?(\d{4})([\x05\x18 ]+[\dA-Fs]*)*([\x00-\x04\x06\x07]|$))#) {
      $vps_data->{vps_time} = $4;
      $vps_data->{new_vps_time} = 1;
      # blank out the same area in the text-only string
      substr($_[0], length($1), length($2), " " x length($2));
      print "VPS time found: $vps_data->{vps_time}\n" if $opt_debug

   # CNI and date "1D102 120406 F9" (ignoring checksum)
   } elsif ($_[0] =~ m#^(.*)([\x05\x18]+([0-9A-F]{2}\d{3})[\x05\x18 ]+(\d{6})([\x05\x18 ]+[\dA-Fs]*)*([\x00-\x04\x06\x07]|$))#) {
      my $cni_str = $3;
      $vps_data->{vps_date} = $4;
      $vps_data->{new_vps_date} = 1;
      substr($_[0], length($1), length($2), " " x length($2));
      $vps_data->{vps_cni} = ConvertVpsCni($cni_str);
      printf("VPS date and CNI: 0x%X, %d\n", $vps_data->{vps_cni}, $vps_data->{vps_date}) if $opt_debug

   # date
   } elsif ($_[0] =~ m#^(.*)([\x05\x18]+(\d{6})([\x05\x18 ]+[\dA-Fs]*)*([\x00-\x04\x06\x07]|$))#) {
      $vps_data->{vps_date} = $3;
      $vps_data->{new_vps_date} = 1;
      substr($_[0], length($1), length($2), " " x length($2));
      print "VPS date: $3\n" if $opt_debug

   # end time (RTL special - not standardized)
   } elsif (!$is_desc &&
            $_[0] =~ m#^(.*)([\x05\x18]+(\d{2}[.:]\d{2}) oo *([\x00-\x04\x06\x07]|$))#) {
      # detected by the OV parser without evaluating the conceal code
      #$vps_data->{page_end_time} = $3;
      #print "VPS(pseudo) page end time: $vps_data->{page_end_time}\n" if $opt_debug

   # "VPS" marker string
   } elsif ($_[0] =~ m#^(.*)([\x05\x18][\x05\x18 ]*VPS *([\x00-\x04\x06\x07]|$))#) {
      substr($_[0], length($1), length($2), " " x length($2));

   # TODO: KiKa special: "VPS 20.00" (magenta)

   } else {
      print "VPS label unrecognized in line \"$_[0]\"\n" if $opt_debug;

      # replace all concealed text with blanks (may also be non-VPS related, e.g. HR3: "Un", "Ra" - who knows what this means to tell us)
      # FIXME text can also be concealed by setting fg := bg (e.g. on desc pages of MDR)
      if ($_[0] =~ m#^(.*)(\x18[^\x00-\x07\x10-\x17]*)#) {
         substr($_[0], length($1), length($2), " " x length($2));
      }
   }
}

# ------------------------------------------------------------------------------
# Convert a "traditional" VPS CNI into a 16-bit PDC channel ID
# - first two digits are the PDC country code in hex (e.g. 1D,1A,24 for D,A,CH)
# - 3rd digit is an "address area", indicating network code tables 1 to 4
# - the final 3 digits are decimal and the index into the network table
#
sub ConvertVpsCni {
   my($cni_str) = @_;

   if ($cni_str =~ m#([0-9A-F]{2})([1-4])(\d{2})#) {
      return (hex($1)<<8) | ((4 - $2) << 6) | ($3 & 0x3F);
   } else {
      return undef;
   }

}

# ------------------------------------------------------------------------------
# Parse feature indicators in the overview table
# - main task to extrace references to teletext pages with longer descriptions
#   unfortunately these references come in a great varity of formats, so we
#   must mostly rely on the teletext number (risking to strip off 3-digit
#   numbers at the end of titles in rare cases)
# - it's mandatory to remove features becaue otherwise they might show up in
#   the middle of a wrapped title
# - most feature strings are unique enough so that they can be identified
#   by a simple pattern match (e.g. 16:9, UT)
# - TODO: use color to identify features; make a first sweep to check if
#   most unique features always have a special color (often yellow) and then
#   remove all words in that color inside title strings

# Examples:
#
# 06.05 # logo!                     >331
#
# 13.15  1315  Heilkraft aus der Wüste   
#              (1/2) Geheimnisse der     
#              Buschmänner 16:9 oo       
#
# TODO: some tags need to be localized (i.e. same as month names etc.)
my %FeatToFlagMap =
(
   "untertitel" => "has_subtitles",
   "ut" => "has_subtitles",
   "omu" => "is_omu",
   "sw" => "is_bw",
   "s/w" => "is_bw",
   "16:9" => "is_aspect_16_9",
   "oo" => "is_stereo",
   "stereo" => "is_stereo",
   "2k" => "is_2chan",
   "2k-ton" => "is_2chan",
   "dolby" => "is_dolby",
   "surround" => "is_dolby",
   "stereo" => "is_stereo",
   "mono" => "is_mono",
   "tipp" => "is_top",
   # ORF
   "°°" => "is_stereo",
   "°*" => "is_2chan",
   "ds" => "is_dolby",
   "ss" => "is_dolby",
   "dd" => "is_dolby",
   "zs" => "is_2chan"
);
# note: must correct $n below if () are added to pattern
my $FeatPat = "UT(( auf | )?[1-8][0-9][0-9])?|".
              "[Uu]ntertitel|[Hh]örfilm|HF|".
              "s\/?w|S\/?W|tlw. s\/w|AD|oo|°°|°\*|OmU|16:9|".
              "2K|2K-Ton|[Mm]ono|[Ss]tereo|[Dd]olby|[Ss]urround|".
              "DS|SS|DD|ZS|".
              "Wh\.?|Wdh\.?|Whg\.?|Tipp\!?";

sub MapTrailingFeat {
   my ($feat, $slot, $orig_title) = @_;

   $feat =~ s#^ut.*#ut#;
   my $flag = $FeatToFlagMap{$feat};
   if (defined($flag)) {
      print "FEAT \"$feat\" -> $flag on TITLE $orig_title\n" if $opt_debug;
      $slot->{$flag} = 1;
   } else {
      print "FEAT dropping \"$feat\" on TITLE $orig_title\n" if $opt_debug;
   }
}

sub ParseTrailingFeat {
   my ($title, $slot) = @_;
   (my $orig_title = $title) =~ s#[\x00-\x1F\x7F]# #g;

   # teletext reference (there's at most one at the very right, so parse this first)
   if ($title =~ s#( \.+|\.\.+|\.+\>|\>\>?|\-\-?\>|\>{1,4} +|\.* +|[\.\>]*[\x00-\x07\x1D]+)([1-8][0-9][0-9]) {0,3}$##) {
      my $ref = $2;
      $slot->{ttx_ref} = ((ord(substr($ref, 0, 1)) - 0x30)<<8) |
                             ((ord(substr($ref, 1, 1)) - 0x30)<<4) |
                             (ord(substr($ref, 2, 1)) - 0x30);
      print "FEAT TTX ref \"$ref\" on TITLE $orig_title\n" if $opt_debug;
   }

   # note: remove trailing space here, not in the loop to allow max. 1 space between features
   $title =~ s#[ \x00-\x1F\x7F]+$##;

   # ARTE: (oo) (2K) (Mono)
   # SWR: "(16:9/UT)", "UT 150", "UT auf 150"
   if ($title =~ m#^(.*)[ \x00-\x07\x1D]\((($FeatPat)(( ?\/ ?|, ?| )($FeatPat))*)\)$#) {
      $title = $1;
      my $feat_list = substr($2, length($3));

      MapTrailingFeat(lc($3), $slot, $orig_title);

      if (length $feat_list)  {
         while ($feat_list =~ s#( ?\/ ?|, ?| )($FeatPat)$##) {
            MapTrailingFeat(lc($2), $slot, $orig_title);
         } 
      } else {
         while ($title =~ s#[ \x00-\x07\x1D]*\(($FeatPat)\)$##) {
            MapTrailingFeat(lc($1), $slot, $orig_title);
         }
      }

   } elsif ($title =~ m#^(.*)(  +|[\x00-\x07\x1D]+)(($FeatPat)((, ?|\/ ?| |[,\/]?[\x00-\x07\x1D]+)($FeatPat))*)$#) {
      $title = $1;
      my $feat_list = substr($3, length($4));
      MapTrailingFeat(lc($4), $slot, $orig_title);

      while ($feat_list =~ s#(, ?|\/ ?| |[,\/]?[\x00-\x07\x1D]+)($FeatPat)$##) {
         MapTrailingFeat(lc($2), $slot, $orig_title);
      } 
   } #else { print "NONE $orig_title\n"; }

   $title =~ s#[\x00-\x1F\x7F]# #g;
   $title =~ s#^ +##;
   $title =~ s# +$##;
   $title =~ s#  +# #g;
   return $title;
}

# ------------------------------------------------------------------------------
# Parse programme overview table
# - 1: retrieve programme list (i.e. start times and titles)
# - 2: retrieve date from the page header
# - 3: determine dates
# - 4: determine stop times
# 
sub ParseOv {
   my($page, $sub, $fmt, $pgdate, $prev_pgdate, $prev_slot_idx, $PrevSlots) = @_;
   my ($day_of_month, $month, $year);
   my @Slots;

   printf("OVERVIEW PAGE %03X.%04X\n", $page, $sub) if $opt_debug;

   PageToLatin($page, $sub);

   my $foot = ParseFooterByColor($page, $sub);

   @Slots = ParseOvList($page, $sub, $foot, $fmt, $pgdate);
   if ($#Slots >= 0) {
      ParseOvHeaderDate($page, $sub, $pgdate);
      if (CalculateDates($pgdate, $prev_pgdate, $prev_slot_idx, $PrevSlots, \@Slots) > 0) {

         # guess missing stop time for last slot of the previous page
         if ( defined($prev_pgdate) &&
              CheckIfPagesAdjacent($prev_pgdate, $pgdate) ) {
            my @TmpList = ( $PrevSlots->[$#$PrevSlots], $Slots[0] );
            CalculateStopTimes(\@TmpList);
         }

         # guess missing stop times for the current page
         CalculateStopTimes(\@Slots);

         # check if this subpage has any new content
         # (some networks use subpages just to display different ads)
         if ( ($sub > 1) && defined($prev_pgdate) &&
              ($prev_pgdate->{page} == $page) &&
              ($prev_pgdate->{sub_page} + $prev_pgdate->{sub_page_skip} + 1 == $sub) &&
              CheckRedundantSubpage(\@Slots, $PrevSlots, $prev_slot_idx) ) {
            # redundant -> discard content of this page
            @Slots = ();
            $prev_pgdate->{sub_page_skip} += 1;
         }
      } else {
         # failed to determine the date
         @Slots = ();
      }
   }
   return @Slots;
}

# ------------------------------------------------------------------------------
# Retrieve programme data from an overview page
# - 1: compare several overview pages to identify the layout
# - 2: parse all overview pages, retrieving titles and ttx references
#
sub ParseAllOvPages {
   my $page;
   my ($sub, $sub1, $sub2);
   my %PgDate;
   my ($pgdate, $prev_pgdate, $prev_slot_idx);
   my @Slots = ();
   my @TmpSlots;
   my $slot;

   my $fmt = DetectOvFormat();
   if ($fmt) {

      my @Pages = grep {($_>=$opt_tv_start) && ($_<=$opt_tv_end)} keys(%PgCnt);
      @Pages = sort {$a<=>$b} @Pages;

      foreach $page (@Pages) {
         if ($PgSub{$page} == 0) {
            $sub1 = $sub2 = 0;
         } else {
            $sub1 = 1;
            $sub2 = $PgSub{$page};
         }
         for ($sub = $sub1; $sub <= $sub2; $sub++) {
            if (TtxSubPageDefined($page, $sub)) {
               $pgdate = {'page' => $page, 'sub_page' => $sub, 'sub_page_skip' => 0 };
               @TmpSlots = ParseOv($page, $sub, $fmt, $pgdate, $prev_pgdate, $prev_slot_idx, \@Slots);
            } else {
               @TmpSlots = ();
            }

            if ($#TmpSlots >= 0) {
               $prev_pgdate = $pgdate;
               $prev_slot_idx = $#Slots + 1;

               push @Slots, @TmpSlots;

            } elsif ( defined($prev_pgdate) &&
                      ($prev_pgdate->{sub_page} + $prev_pgdate->{sub_page_skip} < $sub) ) {
               $prev_pgdate = $prev_slot_idx = undef;
            }
         }
      }
   } else {
      print STDERR "No overview pages found\n";
   }

   # retrieve descriptions from references teletext pages
   foreach $slot (@Slots) {
      if (defined($slot->{ttx_ref})) {
         $slot->{desc} = ParseDescPage($slot);
      }
   }

   return \@Slots;
}

# ------------------------------------------------------------------------------
# Determine dates of programmes on an overview page
# - TODO: arte: remove overlapping: the first one encloses multiple following programmes; possibly recognizable by the VP label 2500
#  "\x1814.40\x05\x182500\x07\x07THEMA: DAS \"NEUE GROSSE   ",
#  "              SPIEL\"                    ",
#  "\x0714.40\x05\x051445\x02\x02USBEKISTAN - ABWEHR DER   ",
#  "            \x02\x02WAHABITEN  (2K)           ",
#
sub CalculateDates {
   my ($pgdate, $prev_pgdate, $prev_slot_idx, $PrevSlots, $Slots) = @_;
   my $retval = 0;

   my $date_off = 0;
   if (defined($prev_pgdate)) {
      my $prev_slot1 = $PrevSlots->[$prev_slot_idx];
      my $prev_slot2 = $PrevSlots->[$#$PrevSlots];
      # check if the page number of the previous page is adjacent
      # and as consistency check require that the prev page covers less than a day
      if ( ($prev_slot2->{start_t} - $prev_slot1->{start_t} < 22*60*60) &&
           CheckIfPagesAdjacent($prev_pgdate, $pgdate) ) {

         my $prev_delta = 0;
         # check if there's a date on the current page
         # if yes, get the delta to the previous one in days (e.g. tomorrow - today = 1)
         # if not, silently fall back to the previous page's date and assume date delta zero
         if (defined($pgdate->{year})) {
            $prev_delta = CalculateDateDelta($prev_pgdate, $pgdate);
         }
         if ($prev_delta == 0) {
            # check if our start date should be different from the one of the previous page
            # -> check if we're starting on a new date (smaller hour)
            # (note: comparing with the 1st slot of the prev. page, not the last!)
            if ($Slots->[0]->{hour} < $prev_slot1->{hour}) {
               # TODO: check continuity to slot2: gap may have 6h max.
               # TODO: check end hour
               $date_off = 1;
            }
            # else: same date as the last programme on the prev page
            # may have been a date change inside the prev. page but our header date may still be unchanged
            # historically this is only done up to 6 o'clock (i.e. 0:00 to 6:00 counts as the old day)
            #} elsif ($Slots->[0]->{hour} <= 6) {
            #   # check continuity
            #   if ( defined($prev_slot2->{end_hour}) ?
            #          ( ($Slots->[0]->{hour} >= $prev_slot2->{end_hour}) &&
            #            ($Slots->[0]->{hour} - $prev_slot2->{end_hour} <= 1) ) :
            #          ( ($Slots->[0]->{hour} >= $prev_slot2->{hour}) &&
            #            ($Slots->[0]->{hour} - $prev_slot2->{hour} <= 6) ) ) {
            #      # OK
            #   } else {
            #      # 
            #   }
            #}
            # TODO: check for date change within the previous page
         } else {
            $prev_pgdate = undef;
         }
         # TODO: date may also be wrong by +1 (e.g. when starting at 23:55 with date for 00:00)
      } else {
         # not adjacent -> disregard the info
         printf("OV DATE: prev page %03X.%04X not adjacent - not used for date check\n", $prev_pgdate->{page}, $prev_pgdate->{sub_page}) if $opt_debug;
         $prev_pgdate = undef;
      }
   }

   my $day_of_month;
   my $month;
   my $year;

   if (defined($pgdate->{year})) {
      $day_of_month = $pgdate->{day_of_month};
      $month = $pgdate->{month};
      $year = $pgdate->{year};
      # store date offset in page meta data (to calculate delta of subsequent pages)
      $pgdate->{date_off} = $date_off;
      print "OV DATE: using page header date $day_of_month.$month.$year, DELTA $date_off\n" if $opt_debug;

   } elsif (defined($prev_pgdate)) {
      # copy date from previous page
      $pgdate->{day_of_month} = $day_of_month = $prev_pgdate->{day_of_month};
      $pgdate->{month} = $month = $prev_pgdate->{month};
      $pgdate->{year} = $year = $prev_pgdate->{year};
      # add date offset if a date change was detected
      $date_off += $prev_pgdate->{date_off};
      $pgdate->{date_off} = $date_off;
      print "OV DATE: using predecessor date $day_of_month.$month.$year, DELTA $date_off\n" if $opt_debug;
   }

   # finally assign the date to all programmes on this page (with auto-increment at hour wraps)
   if (defined($year)) {
      my $prev_hour;
      for (my $idx = 0; $idx <= $#$Slots; $idx++) {
         my $slot = $Slots->[$idx];

         # detect date change (hour wrap at midnight)
         if (defined($prev_hour) && ($prev_hour > $slot->{hour})) {
            $date_off += 1;
         }
         $prev_hour = $slot->{hour};

         $slot->{date_off} = $date_off;
         $slot->{day_of_month} = $pgdate->{day_of_month};
         $slot->{month} = $pgdate->{month};
         $slot->{year} = $pgdate->{year};

         $slot->{start_t} = ConvertStartTime($slot);
      }
      $retval = 1;
   } else {
      print "OV missing date - discarding programmes\n" if $opt_debug;
   }
   return $retval;
}

# ------------------------------------------------------------------------------
# Check if two given teletext pages are adjacent
# - both page numbers must have decimal digits only (i.e. match /[1-8][1-9][1-9]/)
#
sub CheckIfPagesAdjacent {
   my ($pg1, $pg2) = @_;
   my $result = 0;

   if ( ($pg1->{page} == $pg2->{page}) &&
        ($pg1->{sub_page} + $pg1->{sub_page_skip} + 1 == $pg2->{sub_page}) ) {
      # next sub-page on same page
      $result = 1;

   } else {
      # check for jump from last sub-page of prev page to first of new page
      my $next = GetNextPageNumber($pg1->{page});

      if ( ($next == $pg2->{page}) &&
           ($pg1->{sub_page} + $pg1->{sub_page_skip} == $PgSub{$pg1->{page}}) &&
           ($pg2->{sub_page} <= 1) ) {
         $result = 1;
      } 
   }
   return $result;
}

# ------------------------------------------------------------------------------
# Calculate number of page following the previous one
# - basically that's a trivial increment by 1, however page numbers are
#   actually hexedecinal numbers, where the numbers with non-decimal digits
#   are skipped (i.e. not intended for display)
# - hence to get from 9 to 0 in the 2nd and 3rd digit we have to add 7
#   instead of just 1.
#
sub GetNextPageNumber {
   my ($page) = @_;
   my $next;

   if (($page & 0xF) == 9) {
      $next = $page + (0x010 - 0x009);
   } else {
      $next = $page + 1;
   }
   # same skip for the 2nd digit, if there was a wrap in the 3rd digit
   if (($next & 0xF0) == 0x0A0) {
      $next = $next + (0x100 - 0x0A0);
   }
   return $next;
}

# ------------------------------------------------------------------------------
# Determine stop times
# - assuming that in overview tables the stop time is equal to the start of the
#   following programme & that this also holds true inbetween adjacent pages
# - if in doubt, leave it undefined (this is allowed in XMLTV)
# - TODO: restart at non-adjacent text pages
# 
sub CalculateStopTimes {
   my($arr_ref) = @_;
   my $arr_len = $#{$arr_ref} + 1;
   my ($next, $slot);
   my $idx;

   for ($idx = 0; $idx < $arr_len; $idx++) {
      $slot = $arr_ref->[$idx];

      if (!defined($slot->{stop_t})) {
         if (defined($slot->{end_min})) {
            # there was an end time in the overview or a description page -> use that
            my $date_off = $slot->{date_off};
            # check for a day break between start and end
            if ( ($slot->{end_hour} < $slot->{hour}) ||
                 ( ($slot->{end_hour} == $slot->{hour}) &&
                   ($slot->{end_min} < $slot->{min}) )) {
               $date_off += 1;
            }

            $slot->{stop_t} = POSIX::mktime(0, $slot->{end_min}, $slot->{end_hour},
                                               $slot->{day_of_month} + $date_off,
                                               $slot->{month} - 1,
                                               $slot->{year} - 1900,
                                               0, 0, -1);
         } elsif ($idx + 1 < $arr_len) {
            # no end time: use start time of the following programme if less than 9h away
            $next = $arr_ref->[$idx + 1];
            if ( ($next->{start_t} > $slot->{start_t}) &&
                 ($next->{start_t} - $slot->{start_t} < 9*60*60) ) {

               $slot->{stop_t} = $next->{start_t};
            }
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Convert discrete start times into UNIX epoch format
# - implies a conversion from local time zone into GMT
#
sub ConvertStartTime {
   my ($slot) = @_;

   return POSIX::mktime(0, $slot->{min}, $slot->{hour},
                           $slot->{day_of_month} + $slot->{date_off},
                           $slot->{month} - 1,
                           $slot->{year} - 1900,
                           0, 0, -1);
}

# ------------------------------------------------------------------------------
# Calculate delta in days between the given programme slot and a discrete date
# - works both on program "slot" and "pgdate" which have the same member names
#   (since Perl doesn't have strict typechecking for structs)
#
sub CalculateDateDelta {
   my ($slot1, $slot2) = @_;

   my $date1 = POSIX::mktime(0, 0, 0, $slot1->{day_of_month} + $slot1->{date_off},
                             $slot1->{month} - 1, $slot1->{year} - 1900, 0, 0, -1);

   my $date2 = POSIX::mktime(0, 0, 0, $slot2->{day_of_month} + $slot2->{date_off},
                             $slot2->{month} - 1, $slot2->{year} - 1900, 0, 0, -1);

   # add 2 hours to allow for shorter days during daylight saving time change
   return int(($date2 + 2*60*60 - $date1) / (24*60*60));
}

# ------------------------------------------------------------------------------
# Determine LTO for a given time and date
# - LTO depends on the local time zone and daylight saving time being in effect or not
#
sub DetermineLto {
   my ($time_t) = @_;
   my ($sec, $min, $hour, $mday, $mon, $year, @foo);
   my $gmt;
   my $lto;

   # LTO := Difference between GMT and local time
   # (note: for this to work it's mandatory to pass is_dst=-1 to mktime)
   ($sec,$min,$hour,$mday,$mon,$year,@foo) = gmtime($time_t);
   $gmt = mktime($sec, $min, $hour, $mday, $mon, $year, 0, 0, -1);

   if (defined($gmt)) {
      $lto = $time_t - $gmt;
   } else {
      $lto = 0;
   }
   return $lto;
}

# ------------------------------------------------------------------------------
# Filter out expired programmes
# - the stop time is relevant for expiry (and this really makes a difference if
#   the expiry time is low (e.g. 6 hours) since there may be programmes exceeding
#   it and we certainly shouldn't discard programmes which are still running
# - resulting problem: we don't always have the stop time
#
sub FilterExpiredSlots {
   my($Slots) = @_;
   my $exp_thresh = time - $opt_expire * 60;
   my @NewSlots = ();

   foreach (@$Slots) {
      if ( (defined($_->{stop_t}) && ($_->{stop_t} >= $exp_thresh)) ||
           ($_->{start_t} + 120*60 >= $exp_thresh) ) {
         push @NewSlots, $_;
      }
   }
   return \@NewSlots;
}

# ------------------------------------------------------------------------------
# Check if an overview page has exactly the same title list as the predecessor
#
sub CheckRedundantSubpage {
   my ($Slots, $PrevSlots, $prev_slot_idx) = @_;
   my $result = 0;

   if ($#$PrevSlots - $prev_slot_idx == $#$Slots) {
      $result = 1;
      for (my $idx = 0; $idx <= $#$Slots; $idx++) {
         if ($PrevSlots->[$prev_slot_idx + $idx]->{start_t} != $Slots->[$idx]->{start_t}) {
            # TODO compare title
            $result = 0;
            last;
         }
      }
   }
   return $result;
}

# ------------------------------------------------------------------------------
# Export programme data into XMLTV format
#
sub ExportTitle {
   my($slot_ref, $ch_id) = @_;

   if (defined($slot_ref->{title}) &&
       defined($slot_ref->{day_of_month}) &&
       defined($slot_ref->{month}) &&
       defined($slot_ref->{year}) &&
       defined($slot_ref->{hour}) &&
       defined($slot_ref->{min}))
   {
      my $start_str = GetXmlTimestamp($slot_ref->{start_t});
      my $stop_str;
      my $vps_str;
      if (defined $slot_ref->{stop_t}) {
         $stop_str = " stop=\"". GetXmlTimestamp($slot_ref->{stop_t}) ."\"";
      } else {
         $stop_str = "";
      }
      if (defined($slot_ref->{vps_time})) {
         $vps_str = " pdc-start=\"". GetXmlVpsTimestamp($slot_ref) ."\"";
      } else {
         $vps_str = "";
      }
      my $title = $slot_ref->{title};
      # convert all-upper-case titles to lower-case
      # (unless only one consecutive letter, e.g. movie "K2" or "W.I.T.C.H.")
      # TODO move this into separate post-processing stage; make statistics for all titles first
      if ( ($title !~ m#[[:lower:]]#) && ($title =~ m#[[:upper:]]{2}#) ) {
         $title = lc($title);
      }
      Latin1ToXml($title);

      print "\n<programme start=\"$start_str\"$stop_str$vps_str channel=\"$ch_id\">\n".
            "\t<title>$title</title>\n";
      if (defined($slot_ref->{subtitle})) {
         # repeat the title because often the subtitle is actually just the 2nd part
         # of an overlong title (there's no way to distinguish this from series subtitles etc.)
         # TODO: some channel distinguish title from subtitle by making title all-caps (e.g. TV5)
         my $subtitle = $slot_ref->{title} ." ". $slot_ref->{subtitle};
         Latin1ToXml($subtitle);
         print "\t<sub-title>$subtitle</sub-title>\n";
      }
      if (defined($slot_ref->{ttx_ref})) {
         if (defined($slot_ref->{desc}) && ($slot_ref->{desc} ne "")) {
            printf("\t<!-- TTX %03X -->\n", $slot_ref->{ttx_ref});
            my $desc = $slot_ref->{desc};
            Latin1ToXml($desc);
            print "\t<desc>". $desc ."</desc>\n";
         } else {
            # page not captured or no matching date/title found
            printf("\t<desc>(teletext %03X)</desc>\n", $slot_ref->{ttx_ref});
         }
      }
      # video
      if (defined($slot_ref->{is_bw})||defined($slot_ref->{is_aspect_16_9})) {
         print "\t<video>\n";
         if (defined($slot_ref->{is_bw})) {
            print "\t\t<colour>no</colour>\n";
         }
         if (defined($slot_ref->{is_aspect_16_9})) {
            print "\t\t<aspect>16:9</aspect>\n";
         }
         print "\t</video>\n";
      }
      # audio
      if (defined($slot_ref->{is_dolby})) {
         print "\t<audio>\n\t\t<stereo>surround</stereo>\n\t</audio>\n";
      } elsif (defined($slot_ref->{is_stereo})) {
         print "\t<audio>\n\t\t<stereo>stereo</stereo>\n\t</audio>\n";
      } elsif (defined($slot_ref->{is_mono})) {
         print "\t<audio>\n\t\t<stereo>mono</stereo>\n\t</audio>\n";
      }
      # subtitles
      if (defined($slot_ref->{is_omu})) {
         print "\t<subtitles type=\"onscreen\"/>\n";
      } elsif (defined($slot_ref->{has_subtitles})) {
         print "\t<subtitles type=\"teletext\"/>\n";
      }
      # tip/highlight (ARD only)
      if (defined($slot_ref->{is_tip})) {
         print "\t<star-rating>\n\t\t<value>1/1</value>\n\t</star-rating>\n";
      }
      print "</programme>\n";

   } else {
      print "SKIPPING PARTIAL $slot_ref->{title} $slot_ref->{hour}.$slot_ref->{min}\n" if $opt_debug;
   }
}

# ------------------------------------------------------------------------------
# Convert a UNIX epoch timestamp into XMLTV format
#
sub GetXmlTimestamp {
   my ($time_t) = @_;
   my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday);
   my $t_str;

   # get GMT in broken-down format
   ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday) = gmtime($time_t);

   $t_str = POSIX::strftime("%Y%m%d%H%M%S +0000",
                            0, $min, $hour,
                            $mday,$mon,$year,
                            $wday, $yday, -1);

   return $t_str;
}

sub GetXmlVpsTimestamp {
   my ($slot) = @_;
   my $vps_str;
   my $lto = DetermineLto($slot->{start_t});

   if (defined($slot->{vps_date})) {
      $slot->{vps_date} =~ m#(\d{2})(\d{2})(\d{2})#;
      $vps_str = sprintf("%04d%02d%02d", $3 + 2000, $2, $1);
   } else {
      my ($mday,$mon,$year) = (localtime($slot->{start_t}))[3,4,5];
      $vps_str = sprintf("%04d%02d%02d", $year + 1900, $mon + 1, $mday);
   }
   $vps_str .= $slot->{vps_time} .
               sprintf("00 %+03d%02d", $lto / (60*60), abs($lto / 60) % 60);
}

# ------------------------------------------------------------------------------
# Convert a text string for inclusion in XML as CDATA
# - the result is not suitable for attributes since quotes are not escaped
# - input must already have teletext control characters removed
#
sub Latin1ToXml {
   # note: & replacement must be first
   $_[0] =~ s#&#\&amp\;#g;
   $_[0] =~ s#<#\&lt\;#g;
   $_[0] =~ s#>#\&gt\;#g;
   return $_[0];
}

sub XmlToLatin1 {
   $_[0] =~ s#\&gt\;#>#g;
   $_[0] =~ s#\&lt\;#<#g;
   $_[0] =~ s#\&amp\;#\&#g;
   return $_[0];
}

# ------------------------------------------------------------------------------
# Parse description pages
#

#ARD:
#                          Mi 12.04.06 Das Erste 
#                                                
#         11.15 - 12.00 Uhr                      
#         In aller Freundschaft           16:9/UT
#         Ehen auf dem Prüfstand                 

#ZDF:
#  ZDFtext          ZDF.aspekte
#  Programm  Freitag, 21.April   [no time!]

#MDR:
#Mittwoch, 12.04.2006, 14.30-15.30 Uhr  
#                                        
# LexiTV - Wissen für alle               
# H - wie Hühner                        

#3sat:
#      Programm         Heute            
#      Mittwoch, 12. April               
#                         1D107 120406 3B
#  07.00 - 07.30 Uhr            VPS 0700 
#  nano Wh.                              

# Date format:
# ARD:   Fr 14.04.06 // 15.40 - 19.15 Uhr
# ZDF:   Freitag, 14.April // 15.35 - 19.00 Uhr
# Sat.1: Freitag 14.04. // 16:00-17:00
# RTL:   Fr[.] 14.04. 13:30 Uhr
# Pro7:  So 16.04 06:32-07:54 (note: date not on 2/2, only title rep.)
# RTL2:  Freitag, 14.04.06; 12.05 Uhr (at bottom) (1/6+2/6 for movie)
# VOX:   Fr 18:00 Uhr
# MTV:   Fr 14.04 18:00-18:30 // Sa 15.04 18:00-18:30
# VIVA:  Mo - Fr, 20:30 - 21:00 Uhr // Mo - Fr, 14:30 - 15:00 Uhr (also w/o date)
#        Mo 21h; Mi 17h (Wh); So 15h
#        Di, 18:30 - 19:00 h
#        Sa. 15.4. 17:30h
# arte:  Fr 14.04. 00.40 [VPS  0035] bis 20.40 (double-height)
# 3sat:  Freitag, 14. April // 06.20 - 07.00 Uhr
# CNN:   Sunday 23 April, 13:00 & 19:30 CET
#        Saturday 22, Sunday 23 April
#        daily at 11:00 CET
# MDR:   Sonnabend, 15.04.06 // 00.00 - 02.55 Uhr
# KiKa:  14. April 2006 // 06.40-07.05 Uhr
# SWR:   14. April, 00.55 - 02.50 Uhr
# WDR:   Freitag, 14.04.06   13.40-14.40
# BR3:   Freitag, 14.04.06 // 13.30-15.05
#        12.1., 19.45 
# HR3:   Montag 8.45-9.15 Uhr
#        Montag // 9.15-9.45 (i.e. 2-line format)
# Kabl1: Fr 14.04 20:15-21:11
# Tele5: Fr 19:15-20:15

sub ParseDescDate {
   my ($page, $sub, $slot) = @_;
   my ($lmday, $lmonth, $lyear, $lhour, $lmin, $lend_hour, $lend_min);
   my ($new_date, $check_time);
   my $line;

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $lang = $PgLang{$page};
   my $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
   my $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
   my $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);

   # search date and time
   for ($line = 0; $line <= 23; $line++) {
      $_ = $pgtext->[$line];
      $new_date = 0;

      PARSE_DATE: {
         # [Fr] 14.04.[06] (year not optional for single-digit day/month)
         if ( m#(^| )(\d{1,2})\.(\d{1,2})\.(\d{4}|\d{2})?( |,|;|\:|$)# ||
              m#(^| )(\d{1,2})\.(\d)\.(\d{4}|\d{2})( |,|;|\:|$)# ||
              m#(^| )(\d{1,2})\.(\d)\.?(\d{4}|\d{2})?(,|;| ?-)? +\d{1,2}[\.\:]\d{2}(h| |,|;|\:|$)#) {
            my @NewDate = CheckDate($2, $3, $4, undef, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # Fr 14.04 (i.e. no dot after month)
         # here's a risk to match on a time, so we must require a weekday name
         if (m#(^| )($wday_match)(, ?| | - )(\d{1,2})\.(\d{1,2})( |\.|,|;|$)#i ||
             m#(^| )($wday_abbrv_match)(\.,? ?|, ?| - | )(\d{1,2})\.(\d{1,2})( |\.|,|;|\:|$)#i) {
            my @NewDate = CheckDate($4, $5, undef, $2, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # 14.[ ]April [2006]
         if ( m#(^| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|;|\:|$)#i) {
            my @NewDate = CheckDate($2, undef, $5, undef, $3);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # Sunday 22 April (i.e. no dot after day)
         if ( m#(^| )($wday_match)(, ?| - | )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|;|\:|$)#i ||
              m#(^| )($wday_abbrv_match)(\.,? ?|, ?| ?- ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|;|\:|$)#i) {
            my @NewDate = CheckDate($4, undef, $7, $2, $5);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # TODO: "So, 23." (n-tv)

         # Fr[,] 18:00 [-19:00] [Uhr|h]
         # TODO parse time (i.e. allow omission of "Uhr")
         if (m#(^| )($wday_match)(, ?| - | )(\d{1,2}[\.\:]\d{2}((-| - )\d{1,2}[\.\:]\d{2})?(h| |,|;|\:|$))#i ||
             m#(^| )($wday_abbrv_match)(\.,? ?|, ?| - | )(\d{1,2}[\.\:]\d{2}((-| - )\d{1,2}[\.\:]\d{2})?(h| |,|;|\:|$))#i) {
            my @NewDate = CheckDate(undef, undef, undef, $2, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # TODO: 21h (i.e. no minute value: TV5)

         # TODO: make exact match between VPS date and time from overview page
         # TODO: allow garbage before or after label; check reveal and magenta codes (ETS 300 231 ch. 7.3)
         # VPS label "1D102 120406 F9"
         if (s#^ +[0-9A-F]{2}\d{3} (\d{2})(\d{2})(\d{2}) [0-9A-F]{2} *$##) {
            ($lmday, $lmonth, $lyear) = ($1, $2, $3 + 2000);
            last PARSE_DATE;
         }
      }

      # TODO: time should be optional if only one subpage
      # 15.40 [VPS 1540] - 19.15 [Uhr|h]
      $check_time = 0;
      if (m#(^| )(\d{1,2})[\.\:](\d{1,2})( +VPS +(\d{4}))?(-| - | bis )(\d{1,2})[\.\:](\d{1,2})(h| |,|;|\:|$)# ||
          m#(^| )(\d{2})h(\d{2})( +VPS +(\d{4}))?(-| - )(\d{2})h(\d{2})( |,|;|\:|$)#) {
         my $hour = $2;
         my $min = $3;
         my $end_hour = $7;
         my $end_min = $8;
         # my $vps = $5;
         print "DESC TIME $hour.$min - $end_hour.$end_min\n" if $opt_debug;
         if (($hour < 24) && ($min < 60) &&
             ($end_hour < 24) && ($end_min < 60)) {
            ($lhour, $lmin, $lend_hour, $lend_min) = ($hour, $min, $end_hour, $end_min);
            $check_time = 1;
         }

      # 15.40 (Uhr|h)
      } elsif (($new_date && m#(^| )(\d{1,2})[\.\:](\d{1,2})( |,|;|\:|$)#) ||
               m#(^| )(\d{1,2})[\.\:](\d{1,2}) *$# ||
               m#(^| )(\d{1,2})[\.\:](\d{1,2})( ?h| Uhr)( |,|;|\:|$)# ||
               m#(^| )(\d{1,2})h(\d{2})( |,|;|\:|$)#) {
         my $hour = $2;
         my $min = $3;
         print "DESC TIME $hour.$min\n" if $opt_debug;
         if (($hour < 24) && ($min < 60)) {
            ($lhour, $lmin) = ($hour, $min);
            $check_time = 1;
         }
      }

      if ($check_time && defined($lyear)) {
         # allow slight mismatch of <5min (for ORF2 or TV5: list says 17:03, desc says 17:05)
         my $start_t = POSIX::mktime(0, $lmin, $lhour, $lmday, $lmonth - 1, $lyear - 1900, 0, 0, -1);
         if (defined($start_t) && (abs($start_t - $slot->{start_t}) < 5*60)) {
            # match found
            if (defined($lend_hour) && !defined($slot->{end_hour})) {
               # add end time to the slot data
               # TODO: also add VPS time
               # XXX FIXME: these are never used because stop_t is already calculated!
               $slot->{end_hour} = $lend_hour;
               $slot->{end_min} = $lend_min;
            }
            return 1;
         } else {
            print "MISMATCH: ".GetXmlTimestamp($start_t)." ".GetXmlTimestamp($slot->{start_t})."\n" if $opt_debug;
            $lend_hour = undef;
            if ($new_date) {
               # date on same line as time: invalidate both upon mismatch
               $lyear = undef;
            }
         }
      }
   }
   return 0;
}

sub CheckDate {
   my ($mday, $month, $year, $wday_name, $month_name) = @_;

   # first check all provided values
   return () if (defined($mday) && !(($mday <= 31) && ($mday > 0)));
   return () if (defined($month) && !(($month <= 12) && ($month > 0)));
   return () if (defined($year) && !(($year < 100) || (($year >= 2006) && ($year < 2031))));

   if (defined($wday_name) && !defined($mday)) {

      # determine date from weekday name alone
      my $reldate = GetWeekDayOffset($wday_name);
      return () if $reldate < 0;
      ($mday, $month, $year) = (localtime($VbiCaptureTime + $reldate*24*60*60))[3,4,5];
      $month += 1;
      $year += 1900;
      print "DESC DATE $wday_name $mday.$month.$year\n" if $opt_debug;

   } elsif (defined($mday)) {
      if (defined($wday_name)) {
         $wday_name = lc($wday_name);
         return () if !defined($WDayNames{$wday_name});
      }
      if (!defined($month) && defined($month_name)) {
         $month_name = lc($month_name);
         return () if !defined($MonthNames{$month_name});
         $month = $MonthNames{$month_name}->[1];
      } elsif (!defined($month)) {
         return ();
      }
      if (!defined($year)) {
         $year = (localtime($VbiCaptureTime))[5] + 1900;
      } elsif ($year < 100) {
         my $cur_year = (localtime($VbiCaptureTime))[5] + 1900;
         $year += $cur_year - ($cur_year % 100);
      }
      print "DESC DATE $mday.$month.$year\n" if $opt_debug;
   } else {
      return ();
   }
   return ($mday, $month, $year);
}

sub GetWeekDayOffset {
   my ($wday_name) = @_;
   my $wday_idx;
   my $cur_wday_idx;
   my $reldate = -1;

   $wday_name = lc($wday_name);
   if (defined($wday_name) && defined($WDayNames{$wday_name})) {
      $wday_idx = $WDayNames{$wday_name}->[1];
      my $cur_wday_idx = (localtime($VbiCaptureTime))[6];
      if ($wday_idx >= $cur_wday_idx) {
         $reldate = $wday_idx - $cur_wday_idx;
      } else {
         $reldate = (7 - $cur_wday_idx) + $wday_idx;
      }
   }
   return $reldate;
}

sub ParseDescTitle {
   my ($page, $sub, $slot) = @_;
   my $title;
   my $line;
   my ($l, $l1, $l2);

   my $pgtext = $PageText{$page | ($sub << 12)};

   $title = $slot->{title};
   $l1 = $pgtext->[0];
   $l1 =~ s#(^ +| +$)##g;
   $l1 =~ s#  +##g;

   for ($line = 1; $line <= 23/2; $line++) {
      $l2 = $pgtext->[$line + 1];
      $l2 =~ s#(^ +| +$)##g;
      $l2 =~ s#  +# #g;
      $l = $l1 ." ". $l2;
      #TODO: join lines if l1 ends with "-" ?
      #TODO: correct title/subtitle separation of overview

      # TODO: one of the compared title strings may be all uppercase (SWR)

      if ($l1 =~ m#^ *\Q$title\E#) {
         # Done: discard everything above
         # TODO: back up if there's text in the previous line with the same indentation
         return $line;
      }
      $l1 = $l2;
   }
   #print "NOT FOUND $title\n";
   return 0;
}

# Reformat tables in "cast" format into plain lists. Note this function
# must be called before removing control characters so that columns are
# still aligned. Example:
#
# Dr.Hermann Seidel .. Heinz Rühmann
# Vera Bork .......... Wera Frydtberg
# Freddy Blei ........ Gert Fröbe
# OR
# Regie..............Ute Wieland
# Produktion.........Ralph Schwingel,
#                    Stefan Schubert
#
sub DescFormatCastTable {
   my ($Lines) = @_;
   my %Tabs;
   my $tab_max;
   my $result;

   # step #1: build statistic about lines with ".." (including space before and after)
   foreach (@$Lines) {
      if (m#^((.*?)( ?)(\.\.+)( ?))#) {
         my $rec = join(",", length($1), length($3), length($5));
         $Tabs{$rec}++;
         $tab_max = $rec if !defined($tab_max) || ($Tabs{$tab_max} < $Tabs{$rec});
      }
   }

   # minimum is two lines looking like table rows
   if (defined($tab_max) && ($Tabs{$tab_max} >= 2)) {
      print "DESC reformat table into list: $Tabs{$tab_max} rows, FMT $tab_max\n" if $opt_debug;
      my ($off, $spc1, $spc2) = split(/,/, $tab_max);

      # step #2: find all lines with dots ending at the right column and right amount of spaces
      my $last_row = -1;
      my $row = 0;
      foreach (@$Lines) {
         if ((substr($_, 0, $off) =~ m#^(.*?)( ?)(\.+)( ?)$#) &&
             ((!$2 && !$spc1) || (length($2) == $spc1)) &&
             ((!$4 && !$spc2) || (length($4) == $spc2)) ) {
            my $tab1 = $1;
            my $tab2 = substr($_, $off);
            if ($tab2 =~ m#^[^ .]#) {
               # match -> replace dots with colon
               $tab1 =~ s# *:$##;
               $tab2 =~ s#,? +$##g;
               $_ = "$tab1: $tab2,";
               $last_row = $row;

            } elsif ($last_row > 0) {
               # end of table (non-empty line) -> terminate list with semicolon
               $Lines->[$last_row] =~ s#,$#;#;
               $last_row = -1;
            }
         } elsif (($last_row != -1) &&
                  (substr($_, 0, $off + 1) =~ m#^ +[^ ]$#)) {
            # right-side table column continues (left side empty)
            $Lines->[$last_row] =~ s#,$##;
            my $tab2 = substr($_, $off);
            $tab2 =~ s#,? +$##g;
            $_ = "$tab2,";
            $last_row = $row;

         } elsif ($last_row > 0) {
            # end of table: if paragraph break remove comma; else terminate with semicolon
            if (!m#[^ ]#) {
               $Lines->[$last_row] =~ s#,$##;
            } else {
               $Lines->[$last_row] =~ s#,$#;#;
            }
            $last_row = -1;
         }
         $row++;
      }
      if ($last_row > 0) {
         $Lines->[$last_row] =~ s#,$##;
      }
      $result = 1;
   }
   return $result;
}

sub ParseDescContent {
   my ($page, $sub, $head, $foot) = @_;
   my $vps_data = {};
   my $line;
   my @Lines;

   my $pgctrl = $PageCtrl{$page | ($sub << 12)};
   my $is_nl = 0;
   my $desc = "";

   for ($line = $head; $line <= $foot; $line++) {
      $_ = $pgctrl->[$line];

      # TODO parse features behind date, title or subtitle

      # extract and remove VPS labels and all concealed text
      if (m#[\x05\x18]#) {
         ParseVpsLabel($_, $vps_data, 1);
      }
      s#[\x00-\x1F\x7F]# #g;

      # remove VPS time and date labels
      s#^ +[0-9A-F]{2}\d{3} (\d{2})(\d{2})(\d{2}) [0-9A-F]{2} *$##;
      s# +VPS \d{4} *$##;

      push @Lines, $_;
   }

   DescFormatCastTable(\@Lines);

   foreach (@Lines) {
      # TODO: replace 0x7f (i.e. square) at line start with "-"
      # line consisting only of "-": treat as paragraph break
      s#^ *[\-\_\+]{20,} *$##;

      if (m#[^ ]#) {
         s#(^ +| +$)##g;
         s#  +# #g;
         $desc .= "\n" if $is_nl;
         if (($desc =~ m#\S-$#) && m#^[[:lower:]]#) {
            $desc =~ s#-$##;
            $desc .= $_;
         } elsif ((length($desc) > 0) && !$is_nl) {
            $desc .= " " . $_;
         } else {
            $desc .= $_;
         }
         $is_nl = 0;
      } else {
         # empty line: paragraph break
         if (length($desc) > 0) {
            $is_nl = 1;
         }
      }
   }
   return $desc;
}

# TODO: check for all referenced text pages where no match was found if they
#       describe a yet unknown programme (e.g. next instance of a weekly series)

sub ParseDescPage {
   my ($slot_ref) = @_;
   my ($sub, $sub1, $sub2);
   my $desc = "";
   my $found = 0;
   my $page = $slot_ref->{ttx_ref};

   if ($opt_debug) {
      my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday) = localtime($slot_ref->{start_t});
      printf("DESC parse page %03X: search match for %s \"%s\"\n", $page,
             POSIX::strftime("%d.%m.%Y %H:%M", 0, $min, $hour, $mday,$mon,$year,$wday,$yday,-1),
             $slot_ref->{title});
   }

   if (defined($PgCnt{$page})) {
      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         my($head, $foot, $foot2);

         PageToLatin($page, $sub);
         # TODO: multiple sub-pages may belong to same title, but have no date
         #       caution: multiple pages may also have the same title, but describe different instances of a series

         # TODO: bottom of desc pages may contain a 2nd date/time for repeats (e.g. SAT.1: "Whg. Fr 05.05. 05:10-05:35") - note the different BG color!

         if (ParseDescDate($page, $sub, $slot_ref)) {
            $head = ParseDescTitle($page, $sub, $slot_ref);

            $foot = ParseFooterByColor($page, $sub);
            $foot2 = ParseFooter($page, $sub, $head);
            $foot = ($foot2 < $foot) ? $foot2 : $foot;
            printf("DESC page %03X.%04X match found: lines $head-$foot\n", $page, $sub) if $opt_debug;

            $desc .= "\n" if $found;
            $desc .= ParseDescContent($page, $sub, $head, $foot);

            $found = 1;
         } else {
            printf("DESC page %03X.%04X no match found\n", $page, $sub) if $opt_debug;
            last if $found;
         }
      }
   } else {
      printf("DESC page %03X not found\n", $page) if $opt_debug;
   }
   return $desc;
}

# ------------------------------------------------------------------------------
# Determine channel name and ID from teletext header packets
# - remove clock, date and page number: rest should be channel name
#
sub ParseChannelName {
   my %ChName;
   my $page;
   my ($sub, $sub1, $sub2);
   my $name;
   my $pgn;
   my $wday_match;
   my $wday_abbrv_match;
   my $mname_match;

   my @Pages = grep {((($_>>4)&0xF)<=9) && (($_&0xF)<=9)} keys(%PgCnt);
   @Pages = sort {$a<=>$b} @Pages;
   my $found = 0;
   my $lang = -1;

   foreach $page (@Pages) {
      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         $_ = $Pkg{$page|($sub<<12)}->[0];
         if (defined($_)) {
            if ($PgLang{$page} != $lang) {
               $lang = $PgLang{$page};
               $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
               $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
               $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);
            }
            $_ = TtxToLatin1(substr($_, 8), $lang);
            s#[\x00-\x1F\x7F]+# #g;
            $pgn = sprintf("%03X", $page);
            # remove page number and time (both are required)
            if (s#(^| )\Q$pgn\E( |$)#$1$2# &&
                s# \d{2}[\.\: ]?\d{2}([\.\: ]\d{2}) *$# #) {

               # remove date: "Sam.12.Jan" OR "12 Sa Jan" (VOX)
               s#((($wday_abbrv_match)|($wday_match))(\, ?|\. ?|  ?\-  ?|  ?)?)?\d{1,2}(\.\d{1,2}|[ \.]($mname_match))(\.|[ \.]\d{2,4}\.?)? *$##i ||
                  s#\d{1,2}(\, ?|\. ?|  ?\-  ?|  ?)((($wday_abbrv_match)|($wday_match))(\, ?|\. ?|  ?\-  ?|  ?)?)?(\.\d{1,2}|[ \.]($mname_match))(\.|[ \.]\d{2,4}\.?)? *$##i;
               # remove and compress whitespace
               s#(^ +| +$)##g;
               s#  +# #g;
               # remove possible "text" postfix
               s#[ \.\-]?text$##i; # || s#[ \.\-]?Text .*##;

               if (defined($ChName{$_})) {
                  $ChName{$_} += 1;
                  last if $ChName{$_} >= 100;
               } else {
                  $ChName{$_} = 1;
                  $found = 1;
               }
            }
         }
      }
   }
   if ($found) {
      $name = (sort {$ChName{$b}<=>$ChName{$a}} keys(%ChName))[0];
   } else {
      $name = "";
   }
   return $name;
}

# ------------------------------------------------------------------------------
# Determine channel ID from CNI
#
my %Cni2ChannelId = (
  0x1AC1 => "1.orf.at",
  0x24C2 => "1.tsr.ch",
  0x1AC2 => "2.orf.at",
  0x24C8 => "2.tsr.ch",
  0x1DCB => "3.br-online.de",
  0x1AC3 => "3.orf.at",
  0x1DC7 => "3sat.de",
  0x1D44 => "alpha.br-online.de",
  0x1DC1 => "ard.de",
  0x2C7F => "bbc1.bbc.co.uk",
  0x2C40 => "bbc2.bbc.co.uk",
  0x2C11 => "channel4.com",
  0x1546 => "cnn.com",
  0x5BF2 => "discoveryeurope.com",
  0x1D8D => "dsf.com",
  0x2FE1 => "euronews.net",
  0x1D91 => "eurosport.de",
  0x1DCF => "hr-online.de",
  0x1D92 => "kabel1.de",
  0x1DC9 => "kika.de",
  0x1DFE => "mdr.de",
  0x1D8C => "n-tv.de",
  0x1D7A => "n24.de",
  0x1DD4 => "ndr.de",
  0x1DBA => "neunlive.de",
  0x1DC8 => "phoenix.de",
  0x2487 => "prosieben.ch",
  0x1D94 => "prosieben.de",
  0x1DAB => "rtl.de",
  0x1D8F => "rtl2.de",
  0x1DB9 => "sat1.de",
  0x2C0D => "sky-news.sky.com",
  0x2C0E => "sky-one.sky.com",
  0x1D8A => "superrtl.de",
  0x1DE0 => "swr.de",
  0x1D78 => "tele5.de",
  0x1D88 => "viva.tv",
  0x1D8E => "vox.de",
  0x1DE6 => "wdr.de",
  0x1DC2 => "zdf.de",
);
my %NiToPdcCni =
(
   # Austria
   0x4301, 0x1AC1,
   0x4302, 0x1AC2,
   # Belgium
   0x0404, 0x1604,
   0x3201, 0x1601,
   0x3202, 0x1602,
   0x3205, 0x1605,
   0x3206, 0x1606,
   # Croatia
   # Czech Republic
   0x4201, 0x32C1,
   0x4202, 0x32C2,
   0x4203, 0x32C3,
   0x4211, 0x32D1,
   0x4212, 0x32D2,
   0x4221, 0x32E1,
   0x4222, 0x32E2,
   0x4231, 0x32F1,
   0x4232, 0x32F2,
   # Denmark
   0x4502, 0x2902,
   0x4503, 0x2904,
   0x49CF, 0x2903,
   0x7392, 0x2901,
   # Finland
   0x3581, 0x2601,
   0x3582, 0x2602,
   0x358F, 0x260F,
   # France
   0x330A, 0x2F0A,
   0x3311, 0x2F11,
   0x3312, 0x2F12,
   0x3320, 0x2F20,
   0x3321, 0x2F21,
   0x3322, 0x2F22,
   0x33C1, 0x2FC1,
   0x33C2, 0x2FC2,
   0x33C3, 0x2FC3,
   0x33C4, 0x2FC4,
   0x33C5, 0x2FC5,
   0x33C6, 0x2FC6,
   0x33C7, 0x2FC7,
   0x33C8, 0x2FC8,
   0x33C9, 0x2FC9,
   0x33CA, 0x2FCA,
   0x33CB, 0x2FCB,
   0x33CC, 0x2FCC,
   0x33F1, 0x2F01,
   0x33F2, 0x2F02,
   0x33F3, 0x2F03,
   0x33F4, 0x2F04,
   0x33F5, 0x2F05,
   0x33F6, 0x2F06,
   0xF101, 0x2FE2,
   0xF500, 0x2FE5,
   0xFE01, 0x2FE1,
   # Germany
   0x4901, 0x1DC1,
   0x4902, 0x1DC2,
   0x490A, 0x1D85,
   0x490C, 0x1D8E,
   0x4915, 0x1DCF,
   0x4918, 0x1DC8,
   0x4941, 0x1D41,
   0x4942, 0x1D42,
   0x4943, 0x1D43,
   0x4944, 0x1D44,
   0x4982, 0x1D82,
   0x49BD, 0x1D77,
   0x49BE, 0x1D7B,
   0x49BF, 0x1D7F,
   0x49C7, 0x1DC7,
   0x49C9, 0x1DC9,
   0x49CB, 0x1DCB,
   0x49D4, 0x1DD4,
   0x49D9, 0x1DD9,
   0x49DC, 0x1DDC,
   0x49DF, 0x1DDF,
   0x49E1, 0x1DE1,
   0x49E4, 0x1DE4,
   0x49E6, 0x1DE6,
   0x49FE, 0x1DFE,
   0x49FF, 0x1DCE,
   0x5C49, 0x1D7D,
   # Greece
   0x3001, 0x2101,
   0x3002, 0x2102,
   0x3003, 0x2103,
   # Hungary
   # Iceland
   # Ireland
   0x3531, 0x4201,
   0x3532, 0x4202,
   0x3533, 0x4203,
   # Italy
   0x3911, 0x1511,
   0x3913, 0x1513,
   0x3914, 0x1514,
   0x3915, 0x1515,
   0x3916, 0x1516,
   0x3917, 0x1517,
   0x3918, 0x1518,
   0x3919, 0x1519,
   0x3942, 0x1542,
   0x3943, 0x1543,
   0x3944, 0x1544,
   0x3945, 0x1545,
   0x3946, 0x1546,
   0x3947, 0x1547,
   0x3948, 0x1548,
   0x3949, 0x1549,
   0x3960, 0x1560,
   0x3968, 0x1568,
   0x3990, 0x1590,
   0x3991, 0x1591,
   0x3992, 0x1592,
   0x3993, 0x1593,
   0x3994, 0x1594,
   0x3996, 0x1596,
   0x39A0, 0x15A0,
   0x39A1, 0x15A1,
   0x39A2, 0x15A2,
   0x39A3, 0x15A3,
   0x39A4, 0x15A4,
   0x39A5, 0x15A5,
   0x39A6, 0x15A6,
   0x39B0, 0x15B0,
   0x39B2, 0x15B2,
   0x39B3, 0x15B3,
   0x39B4, 0x15B4,
   0x39B5, 0x15B5,
   0x39B6, 0x15B6,
   0x39B7, 0x15B7,
   0x39B9, 0x15B9,
   0x39C7, 0x15C7,
   # Luxembourg
   # Netherlands
   0x3101, 0x4801,
   0x3102, 0x4802,
   0x3103, 0x4803,
   0x3104, 0x4804,
   0x3105, 0x4805,
   0x3106, 0x4806,
   0x3120, 0x4820,
   0x3122, 0x4822,
   # Norway
   # Poland
   # Portugal
   # San Marino
   # Slovakia
   0x42A1, 0x35A1,
   0x42A2, 0x35A2,
   0x42A3, 0x35A3,
   0x42A4, 0x35A4,
   0x42A5, 0x35A5,
   0x42A6, 0x35A6,
   # Slovenia
   # Spain
   # Sweden
   0x4601, 0x4E01,
   0x4602, 0x4E02,
   0x4640, 0x4E40,
   # Switzerland
   0x4101, 0x24C1,
   0x4102, 0x24C2,
   0x4103, 0x24C3,
   0x4107, 0x24C7,
   0x4108, 0x24C8,
   0x4109, 0x24C9,
   0x410A, 0x24CA,
   0x4121, 0x2421,
   # Turkey
   0x9001, 0x4301,
   0x9002, 0x4302,
   0x9003, 0x4303,
   0x9004, 0x4304,
   0x9005, 0x4305,
   0x9006, 0x4306,
   # UK
   0x01F2, 0x5BF1,
   0x10E4, 0x2C34,
   0x1609, 0x2C09,
   0x1984, 0x2C04,
   0x200A, 0x2C0A,
   0x25D0, 0x2C30,
   0x28EB, 0x2C2B,
   0x2F27, 0x2C37,
   0x37E5, 0x2C25,
   0x3F39, 0x2C39,
   0x4401, 0x5BFA,
   0x4402, 0x2C01,
   0x4403, 0x2C3C,
   0x4404, 0x5BF0,
   0x4405, 0x5BEF,
   0x4406, 0x5BF7,
   0x4407, 0x5BF2,
   0x4408, 0x5BF3,
   0x4409, 0x5BF8,
   0x4440, 0x2C40,
   0x4441, 0x2C41,
   0x4442, 0x2C42,
   0x4444, 0x2C44,
   0x4457, 0x2C57,
   0x4468, 0x2C68,
   0x4469, 0x2C69,
   0x447B, 0x2C7B,
   0x447D, 0x2C7D,
   0x447E, 0x2C7E,
   0x447F, 0x2C7F,
   0x44D1, 0x5BCC,
   0x4D54, 0x2C14,
   0x4D58, 0x2C20,
   0x4D59, 0x2C21,
   0x4D5A, 0x5BF4,
   0x4D5B, 0x5BF5,
   0x5AAF, 0x2C3F,
   0x82DD, 0x2C1D,
   0x82E1, 0x2C05,
   0x833B, 0x2C3D,
   0x884B, 0x2C0B,
   0x8E71, 0x2C31,
   0x8E72, 0x2C35,
   0x9602, 0x2C02,
   0xA2FE, 0x2C3E,
   0xA82C, 0x2C2C,
   0xADD8, 0x2C18,
   0xADDC, 0x5BD2,
   0xB4C7, 0x2C07,
   0xB7F7, 0x2C27,
   0xC47B, 0x2C3B,
   0xC8DE, 0x2C1E,
   0xF33A, 0x2C3A,
   0xF9D2, 0x2C12,
   0xFA2C, 0x2C2D,
   0xFA6F, 0x2C2F,
   0xFB9C, 0x2C1C,
   0xFCD1, 0x2C11,
   0xFCE4, 0x2C24,
   0xFCF3, 0x2C13,
   0xFCF4, 0x5BF6,
   0xFCF5, 0x2C15,
   0xFCF6, 0x5BF9,
   0xFCF7, 0x2C17,
   0xFCF8, 0x2C08,
   0xFCF9, 0x2C19,
   0xFCFA, 0x2C1A,
   0xFCFB, 0x2C1B,
   0xFCFC, 0x2C0C,
   0xFCFD, 0x2C0D,
   0xFCFE, 0x2C0E,
   0xFCFF, 0x2C0F,
   # Ukraine
   # USA
);

sub ParseChannelId {
   my @Cnis = sort { $PkgCni{$b} <=> $PkgCni{$a} } keys %PkgCni;
   if ($#Cnis >= 0) {
      my $cni = $Cnis[0];
      if (defined($NiToPdcCni{$cni})) {
         $cni = $NiToPdcCni{$cni};
      }
      if (($cni >> 8) == 0x0D) {
         $cni |= 0x1000;
      } elsif (($cni >> 8) == 0x0A) {
         $cni |= 0x1000;
      } elsif (($cni >> 8) == 0x04) {
         $cni |= 0x2000;
      }
      if (defined($Cni2ChannelId{$cni})) {
         return $Cni2ChannelId{$cni};
      } else {
         return sprintf "CNI%04X", $Cnis[0];
      }
   } else {
      return "";
   }
}

# ------------------------------------------------------------------------------
# Read an XMLTV input file
# - note this is NOT a full XML parser (not by far)
#   will only work with XMLTV files generated by this grabber
#
sub ImportXmltvFile {
   my ($fname, $MergeProgRef, $MergeChnRef) = @_;
   my %ChnProgDef;
   my $state = 0;
   my $tag_data;
   my $chn_id;
   my $start_t;
   my $exp_thresh = time - $opt_expire * 60;

   open(XML, "<$fname") || die "cannot open merge input file '$fname': $!\n";
   while ($_ = <XML>) {
      # handling XML header
      if ($state == 0) {
         if (/^\s*\<(\?xml|\!)[^>]*\>\s*$/i) {
         } elsif (/^\s*\<tv([^>"=]+(="[^"]*")?)*>\s*$/i) {
            $state = 2;
         } elsif (/\S/) {
            chomp;
            warn "Unexpected line '$_' in $fname\n";
         }
      } elsif ($state == 1) {
         warn "Unexpected line '$_' following </tv> in $fname\n";

      # handling main section in-between <tv> </tv>
      } elsif ($state == 2) {
         if (/^\s*\<\/tv>\s*$/i) {
            $state = 1;
         } elsif (/^\s*\<channel/i) {
            if (/id=\"([^"]*)\"/i) {
               $chn_id = $1;
            } else {
               warn "Missing 'id' attribute in '$_' in $fname\n";
            }
            $tag_data = $_;
            $state = 3;
         } elsif (/^\s*\<programme/i) {
            if (/start=\"([^"]*)\".*channel=\"([^"]*)\"/i) {
               $chn_id = $2;
               $start_t = ParseTimestamp($1);
               die "Unknown channel $chn_id in merge input\n" if !defined($MergeChnRef->{$chn_id});
            } else {
               warn "Missing 'start' and 'channel' attributes in '$_' in $fname\n";
               undef $chn_id;
            }
            $tag_data = $_;
            $state = 4;
         } elsif (/\S/) {
            warn "Unexpected tag '$_' in $fname; expect <channel> or <programme>\n";
         }

      # handling channel data
      } elsif ($state == 3) {
         $tag_data .= $_;
         if (/^\s*\<\/channel>\s*$/i) {
            $MergeChnRef->{$chn_id} = $tag_data;
            $state = 2;
         }

      # handling programme data
      } elsif ($state == 4) {
         $tag_data .= $_;
         if (/^\s*\<\/programme>\s*$/i) {
            if (($start_t >= $exp_thresh) && defined($chn_id)) {
               $$MergeProgRef{"$start_t;$chn_id"} = $tag_data;
               # remember that there's at least one programme for this channel
               $ChnProgDef{$chn_id} = 1;
            }
            $state = 2;
         }
      }
   }
   close(XML);

   # remove empty channels
   foreach (keys %$MergeChnRef) {
      if (!defined($ChnProgDef{$_})) {
         print "XMLTV input: skip empty CHANNEL ID $_\n" if $opt_debug;
         delete $MergeChnRef->{$_};
      }
   }
}

# ------------------------------------------------------------------------------
# Parse an XMLTV timestamp (DTD 0.5)
# - we expect local timezone only (since this is what the ttx grabber generates)
#
sub ParseTimestamp {
   # format "YYYYMMDDhhmmss ZZzzz"
   my ($Y,$M,$D,$h,$m,$s) = ($_[0] =~ /^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2}) \+0000$/);

   # not correct: mktime assumes local timezone
   #return POSIX::mktime($s, $m, $h, $D, $M - 1, $Y - 1900, 0, 0, -1);

   # requires Time::Local module
   return timegm($s,$m,$h,$D,$M - 1,$Y - 1900);
}

# ------------------------------------------------------------------------------
# Merge old and new programmes
#
sub MergeNextSlot {
   my ($Slots, $OldProgHash, $OldProgKeys) = @_;
   my $new_start;
   my $new_stop;
   my $old_start;
   my $old_stop;

   # get the first (oldest) programme start/stop times from both sources
   if ($#{$Slots} >= 0) {
      $new_start = $Slots->[0]->{start_t};
      $new_stop = $Slots->[0]->{stop_t};
      $new_stop = $new_start + 1 if !defined $new_stop;  # FIXME

      # remove overlapping (or repeated) programmes in the new data
      while (($#{$Slots} >= 1) && ($Slots->[1]->{start_t} < $new_stop)) {
         print "MERGE DISCARD NEW $Slots->[1]->{start_t} '$Slots->[1]->{title}' ovl $new_start..$new_stop\n" if $opt_debug;
         splice(@$Slots, 1, 1);
      }
   }
   if ($#{$OldProgKeys} >= 0) {
      $old_start = $OldProgKeys->[0];
      $old_stop = GetXmltvStopTime($OldProgHash->{$old_start}, $old_start);
   }

   if (defined($new_start) && defined($old_start)) {
      print "MERGE CMP $Slots->[0]->{title} -- ".substr(GetXmltvTitle($OldProgHash->{$OldProgKeys->[0]}),0,30)."\n" if $opt_debug;
      # discard old programmes which overlap the next new one
      # TODO: merge description from old to new if time and title are identical and new has no description
      while (($old_start < $new_stop) && ($old_stop > $new_start)) {
         print "MERGE DISCARD OLD $old_start...$old_stop  ovl $new_start..$new_stop\n" if $opt_debug;
         MergeSlotDesc($Slots->[0], $OldProgHash->{$old_start}, $new_start, $new_stop, $old_start, $old_stop);
         shift @$OldProgKeys;
         if ($#{$OldProgKeys} >= 0) {
            $old_start = $OldProgKeys->[0];
            $old_stop = GetXmltvStopTime($OldProgHash->{$old_start}, $old_start);
            print "MERGE CMP $Slots->[0]->{title} -- ".substr(GetXmltvTitle($OldProgHash->{$OldProgKeys->[0]}),0,30)."\n" if $opt_debug;
         } else {
            undef $old_start;
            undef $old_stop;
            last;
         }
      }
      if (!defined($old_start) || ($new_start <= $old_start)) {
         # new programme starts earlier -> choose data from new source
         # discard old programmes which are overlapped the next new one
         while (($#{$OldProgKeys} >= 0) && ($OldProgKeys->[0] < $new_stop)) {
            print "MERGE DISCARD2 OLD $OldProgKeys->[0]  ovl STOP $new_stop\n" if $opt_debug;
            MergeSlotDesc($Slots->[0], $OldProgHash->{$old_start}, $new_start, $new_stop, $old_start, $old_stop);
            shift @$OldProgKeys;
         }
         print "MERGE CHOOSE NEW\n" if $opt_debug;
         return shift @$Slots;
      } else {
         # choose data from old source
         print "MERGE CHOOSE OLD\n" if $opt_debug;
         return shift @$OldProgKeys;
      }

   # special cases: only one source available anymore
   } elsif (defined($new_start)) {
      return shift @$Slots;

   } elsif (defined($old_start)) {
      return shift @$OldProgKeys;

   } else {
      return undef;
   }
}

sub GetXmltvStopTime {
   if ($_[0] =~ /stop=\"([^"]*)\"/) {
      return ParseTimestamp($1);
   } else {
      return $_[1];
   }
}

sub GetXmltvTitle {
   if ($_[0] =~ /<title>(.*)<\/title>/) {
      return XmlToLatin1($_=$1);
   } else {
      return "";
   }
}

sub GetXmltvDesc {
   if ($_[0] =~ /<desc>(.*)<\/desc>/ms) {
      return XmlToLatin1($_=$1);
   } else {
      return "";
   }
}

# ------------------------------------------------------------------------------
# Merge description and other meta-data from old source, if missing in new source
# - called before programmes in the old source are discarded
#
sub MergeSlotDesc {
   my ($new_slot, $old_xml, $new_start, $new_stop, $old_start, $old_stop) = @_;

   if (($old_start == $new_start) && ($old_stop == $new_stop)) {
      my $old_title = GetXmltvTitle($old_xml);

      # FIXME allow for parity errors (& use redundancy to correct them)
      if ($new_slot->{title} eq $old_title) {

         if ( !defined($new_slot->{desc}) || ($new_slot->{desc} eq "") ||
              ($new_slot->{desc} =~ /^\(teletext [1-8][0-9][0-9]\)$/) ) {
            my $old_desc = GetXmltvDesc($old_xml);

            if ($old_desc && ($old_desc !~ /^\(teletext [1-8][0-9][0-9]\)$/)) {

               # copy description
               $new_slot->{desc} = $old_desc;
            }
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Write grabbed data to XMLTV
# - merge with old data, if available
#
sub ExportXmltv {
   my ($ch_id, $ch_name, $NewSlots, $MergeProg, $MergeChn) = @_;
   my $id;

   if (defined($opt_outfile)) {
      close STDOUT;
      open(STDOUT, ">$opt_outfile") || die "Failed to create $opt_outfile: $!\n";
   }

   print "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n".
         "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n";
   if ($opt_verify) {
      print "<tv>\n";
   } else {
      print "<tv generator-info-name=\"$version\" generator-info-url=\"$home_url\" ".
                "source-info-name=\"teletext\">\n";
   }

   # print channel table
   if (!defined($MergeChn->{$ch_id})) {
      print "<channel id=\"$ch_id\">\n\t<display-name>$ch_name</display-name>\n</channel>\n";
   }
   foreach $id (keys %$MergeChn) {
      if ($id eq $ch_id) {
         print "<channel id=\"$ch_id\">\n\t<display-name>$ch_name</display-name>\n</channel>\n";
      } else {
         print $MergeChn->{$id};
      }
   }

   if (defined($MergeChn->{$ch_id})) {
      # extract respective channel's data from merge input
      my %OldProgHash = ();
      foreach (keys %$MergeProg) {
         my ($start, $id) = split(/;/, $_);
         if ($id eq $ch_id) {
            $OldProgHash{$start} = $MergeProg->{$_};
            delete $MergeProg->{$_};
         }
      }
      # sort old programmes by start time
      my @OldProgKeys = sort keys(%OldProgHash);

      # sort new programmes by start time
      my @NewSlotSorted = sort {$a->{start_t} <=> $b->{start_t}} @$NewSlots;

      # combine both sources (i.e. merge them)
      while ($_ = MergeNextSlot(\@NewSlotSorted, \%OldProgHash, \@OldProgKeys)) {
         if (ref($_)) {
            ExportTitle($_, $ch_id);
         } else {
            print "\n". $OldProgHash{$_};
         }
      }

   } else {
      # no merge required -> simply print all new
      my $slot;
      foreach $slot (@$NewSlots) {
         ExportTitle($slot, $ch_id);
      }
   }

   # append data for all remaining old channels unchanged
   foreach (keys(%$MergeProg)) {
      print "\n";
      print $MergeProg->{$_};
   }

   print "</tv>\n";
   close STDOUT;
}

# ------------------------------------------------------------------------------
# Main

# enable locale so that case conversions work outside of ASCII too
setlocale(LC_CTYPE, "");

# parse command line arguments
ParseArgv();

# read teletext data into memory
if ($opt_verify == 0) {
   ReadVbi();
} else {
   ImportRawDump();
}

# debug only: dump teletext data into file
if ($opt_dump == 1) {
   DumpTextPages();

} elsif ($opt_dump == 2) {
   DumpRawTeletext();

} elsif ($opt_dump == 3) {
   # dump already done while capturing

# parse and export programme data
} else {
   my $NewSlots;
   my %MergeProg;
   my %MergeChn;
   my $ch_name;
   my $ch_id;

   # grab new XML data from teletext
   $NewSlots = ParseAllOvPages();

   # remove programmes beyond the expiration threshold
   if ($opt_verify == 0) {
      $NewSlots = FilterExpiredSlots($NewSlots);
   }

   # make sure to never write an empty file
   if ($#{$NewSlots} >= 0) {

      # get channel name from teletext header packets
      $ch_name = $opt_chname;
      $ch_name = ParseChannelName() if !defined $ch_name;

      $ch_id = $opt_chid;
      $ch_id = ParseChannelId() if !defined $ch_id;

      $ch_name = $ch_id if $ch_name eq "";
      $ch_name = "???" if $ch_name eq "";
      $ch_id = $ch_name if $ch_id eq "";

      # read and merge old data from XMLTV file
      if (defined($opt_mergefile)) {
         ImportXmltvFile($opt_mergefile, \%MergeProg, \%MergeChn);
      }

      ExportXmltv($ch_id, $ch_name, $NewSlots, \%MergeProg, \%MergeChn);
      exit 0;

   } else {
      # return error code to signal abormal termination
      exit 100;
   }
}

