Author
------

The WDM interface library was developed by Gerard Chevalier in 2004-2005.

Gerard has stopped development on this project since then.
The library is currently supported by Tom Zoerner.


General Information
-------------------

The VBIAcqWDMDrv.dll DLL is an interface betwen nxtvepg and the TV card's
WDM compliant hardware driver provided by your TV card manufacturer. This
interface relies completely on this hardware driver.

Hence the hardware driver must handle VBI data correctly (the teletext data)
in order to have nxtvepg working properly.

Thus, before trying anything, make sure the TV application that came
with your card is able to display teletext (or any other one known to
work with teletext).  If it isn't the case, there is nearly no chance
that nxtvepg will work.


Installation
------------

Unzip the ZIP file in the directory where nxtvepg is installed,
i.e. the DLL should be in the same directory as nxtvepg.exe.


First launch
------------

Lauch nxtvepg.exe

Go to Configure menu item / TV card input...

Select the WDM device in the "TV card:" list. Choose the device with the
same name as the one displayed in device manager under "Sound, video and
game controllers" (in general the second one in the list).

Click OK.

Go AGAIN to Configure menu item / TV card input... and select (if not
already selected) the video source.

Click OK.

Do as described in manual :

- Configure menu item / Provider scan ... (don't forget to select the
right geographic zone)

- If provider found, do Configure / Select provider ... and choose one
of the provider found

After that, acquisition should start (have a look at Control menu item
View acq statistics...)

In the hope it will work with your TV Card.


IF IT DOESN'T WORK
------------------

If several providers were found, lauch nxtvepg, choose a different one,
and try again.

If it still doesn't work, you may try actions described below.

Unzip the file WDMDebugPkgMMDDYY.zip in the same directory than nxtvepg.exe.


Notice :

The current nxtvepg release is a debug one with a lot of trace prints.
To get the debug messages, FIRST, launch DebugView (download DebugView
from www.sysinternals.com in the utilities section).

Launch Dbgview.exe
Lauch nxtvepg.exe

After 1 min or so, quit nxtvepg, go to Dbgview, and save the output in
a file to give feedback later.

If it still doesn't work, send the file with a description of your
system (including TV card model) to tomzo(AT)users.sf.net with a
description of the problem.


