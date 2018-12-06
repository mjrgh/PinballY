# PinballY

PinballY is a "front end" menu system/game selector for virtual
pinball cabinets.  A front end is a program that displays an
arcade-style interface for scrolling through your virtual tables and
selecting the one you'd like to play next.  It makes your cab
friendlier and gives it more of an arcade feel by hiding the Windows
desktop, allowing you to access all functions through the basic
set of pin cab buttons.

The program is designed to be easy to set up and use.  It can
automatically find your installed games for pinball player systems 
like Visual Pinball and Future Pinball, and it's equipped with
automatic screen image and video capture functions so that you
can easily set up video preview of each game while you're browing.

Please visit http://www.mjrnet.org/pinscape/PinballY.php for more
information.

## Installing

The release builds at the [PinballY site](http://www.mjrnet.org/pinscape/PinballY.php)
include a Windows Setup (MSI) installer as well
as a plain ZIP file distribution.  Installing from the ZIP file is
simply a matter of unpacking the archive into a folder on your
hard disk.  The program is self-contained and doesn't require any
registry settings or other external system changes.  The main reason
to use the Setup version is that it avoids the annoying "blocked file"
security issue that you get on some systems when unpacking Internet ZIP downloads.


**Don't install in Program Files:**
You can install the program in almost any folder, except that you
**shouldn't** put it within the Program Files folder.  The program
writes to its own install folder to store some settings and database
files, in an effort to be self-contained and avoid scattering files
and registry settings across your system.  But the Program Files
folder tree is protected, which makes this kind of self-contained
configuration impossible if you install the program there.  
Please choose a location outside of Program Files, such as **C:\PinballY**.


## Setting up

The default settings are designed to work on most systems "out of
the box", with little or no settings adjustments.  In
particular, the system will try to find installed pinball player
programs like Visual Pinball and Future Pinball based on their
file associations in the registry, so in most cases it should be
able to find and launch your games with no additional setup.  So
you should be able to try out the program without a lot of initial
setup work.

You can customize the screen layout by moving and resizing the windows
in the normal fashion.  Some windows (such as the instruction card and
DMD window) are "borderless", meaning they lack the normal caption bar
and sizing borders.  You can move one of these windows by clicking the
mouse anywhere within the window and dragging it.  These windows also
all have an invisible sizing border around the edges, so you can
resize them by clicking and dragging near any edge.

All windows can be made full-screen on any monitor.  Just position
the window within the monitor that you want it to take over, right-click
on the window, and select Full Screen from the menu.  You can go back
to regular windowed mode with the same menu command.

The program has many other settings that you can customize as well.
Right-click in any window and select Options from the menu.


## Getting help

The program comes with fairly extensive help files.  In the settings
dialog, click the "?" icon at the top of the dialog to bring up help
for the current settings page.  You can also view the table of contents
for the whole help system by right-clicking in any window and selecting
Help from the menu.


## Building

If you want to build the system from source, it's fairly simple to do.
The only build tool required is Visual Studio; I currently use the
free Community Edition of Visual Studio 2017.  You'll also need to set
up a few other Microsoft SDKs, also freely available.  Follow the
instructions in "Build Environment Setup.txt", which you'll find in
the Notes folder after cloning the git.  You might also want to look
through the other files in the Notes folder for more about the
program's internals; some of these are just notes I made for my own
future reference, but some have information on the program's design
that might be helpful if you're planning to do any original work on
the project.
