Installation
------------

Unzip the nxtvepgWDMPatchDDMMYY.zip file in the directory where nxtvepg is installed.


First launch
------------


Lauch nxtvepg.exe

A Dialog Box may say that nxtvepg.ini is not found, ignore it and click OK.
Go to Configure menu item / TV card input...
Select the WDM device in the "TV card:" list. Choose the device with the same name as the one displayed
in device manager under "Sound, video and game controllers" (in general the second one in the list).
Click OK.
Go AGAIN to Configure menu item / TV card input...
Select (if not already selected) the video source.
Click OK.

Do as described in manual :
- Configure menu item / Provider scan ... (don't forget to select the right geographic zone)
- If provider found, do Configure / Select provider ... and choose one of the provider found

and acquisition should start (have a look at Control menu item / View acq statistics...)

In the hope it will work with your TV Card.


IF IT DOESN'T WORK
------------------

If several providers were found, lauch nxtvepg, choose a different one, and try again.

If it still doesn't work, you may try actions described bellow.

Unzip the file WDMDebugPkgDDMMYY.zip in the same directory than nxtvepg.exe.

IMPORTANT
---------
	The current nxtvepg release is a debug one with a lot of trace prints.
    To get the debug messages, FIRST, launch Dbgview.exe (from sysinternals.com)
IMPORTANT
---------

Launch Dbgview.exe
Lauch nxtvepg.exe

After 1 min or so, quit nxtvepg, go to Dbgview, and save the output in a file to give feedback later.

Look in the file for already known problems :
- to come

If it still doesn't work, send the file with a description of your system (including TV card model)
to gchevalier@users.sourceforge.net with a description of the problem.
PLEASE : do not put this address in your address book (or anything else) to avoid spam in case of you get a virus !
THANK YOU.
