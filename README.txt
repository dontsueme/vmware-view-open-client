VMware View Open Client 4.0.0
Build 215529, 2009-12-08

VMware is a registered trademark or trademark (the "Marks") of VMware, Inc.
in the United States and/or other jurisdictions and is not licensed to you
under the terms of the LGPL version 2.1. If you distribute VMware View Open
Client unmodified in either binary or source form or the accompanying
documentation unmodified, you may not remove, change, alter or otherwise
modify the Marks in any manner. If you make minor modifications to VMware
View Open Client or the accompanying documentation, you may, but are not
required to, continue to distribute the unaltered Marks with your binary or
source distributions. If you make major functional changes to VMware View
Open Client or the accompanying documentation, you may not distribute the
Marks with your binary or source distribution and you must remove all
references to the Marks contained in your distribution. All other use or
distribution of the Marks requires the prior written consent of VMware.
All other marks and names mentioned herein may be trademarks of their
respective companies.

Copyright (C) 1998-2009 VMware, Inc. All rights reserved. This product
is protected by U.S. and international copyright and intellectual
property laws. VMware software products are protected by one or more
U.S. Patent Numbers 6,075,938, 6,397,242, 6,496,847, 6,704,925,
6,711,672, 6,725,289, 6,735,601, 6,785,886, 6,789,156, 6,795,966,
6,880,022, 6,944,699, 6,961,806, 6,961,941, 7,069,413, 7,082,598,
7,089,377, 7,111,086, 7,111,145, 7,117,481, 7,149,843, 7,155,558,
7,222,221, 7,260,815, 7,260,820, 7,269,683, 7,275,136, 7,277,998,
7,277,999, 7,278,030, 7,281,102, 7,290,253, 7,343,599, 7,356,679,
7,409,487, 7,412,492, 7,412,702, 7,424,710, 7,428,636, 7,433,951,
7,434,002, 7,447,854, 7,475,002, 7,478,173, 7,478,180, 7,478,218,
7,478,388, 7,484,208, 7,487,313, 7,487,314, 7,490,216, 7,500,048,
7,506,122, 7,516,453, 7,529,897, 7,555,747, 7,577,722, 7,581,064,
7,590,982, 7,594,111, 7,603,704, 7,606,868; patents pending.


WELCOME
-------

Welcome to the release of VMware View Open Client 4.0.0.

For VMware View partners, official builds of VMware View Client are
available through VMware Partner Engineering. If you don't feel
you can use an official build, perhaps because you are targeting a
processor architecture other than x86 or have functional requirements
that don't fit with our official build, we hope that the open source
release will help you produce a custom client. Whether you are using
an official build, a client derived from the View Open Client sources,
or a client written from scratch, contact Partner Engineering to
certify your product as View compatible.

VMware View Open Client is optimized for thin client devices: the
installed disk footprint is less than two megabytes, it has minimal
RAM requirements, and it has only a few library dependencies.

For more details on the features and capabilities of this client, please
refer to the end user documentation and the administrator's guide, both
of which are available in the installation package.


STATUS
------

This is the General Availability release of VMware View Open Client
4.0.0.


CONTACT
-------

Contact information is available at the Google Code site for the
open-source project:

http://code.google.com/p/vmware-view-open-client/


VIEW BROKER COMPATIBILITY
-------------------------

This release is compatible with VDM 2.0, VDM 2.1, View 3.0, View 3.1,
and View 4.0.


SYSTEM REQUIREMENTS
-------------------

VMware View Open Client requires an i586 compatible processor, two
megabytes of secondary storage, and 128 megabytes of RAM.

VMware is actively testing this build against SUSE Linux Enterprise Thin
Client (SLETC) and Debian 4.0r3, however it should work with any Linux
distribution that meets the minimum library requirements listed below.

Required Version    Libraries
----------------    ---------
glibc 2.x           libc.so.6, libdl.so.2
gcc 3.4.x           libstdc++.so.6, libgcc_s.so.1
glib 2.6            libglib-2.0.so.0, libgobject-2.0.so.0
gtk+ 2.4            libgtk-x11-2.0.so.0, libgdk-x11.2.0.so.0,
                    libgdk_pixbuf-2.0.so.0
libpng 1.2.x        libpng12.so.0
openssl 0.9.8       libssl.so.0.9.8, libcrypto.so.0.9.8
libxml 2.6.x        libxml2.so.2
zlib 1.2.3          libz.so.1

In addition, rdesktop is required for RDP
connections. VMware View Open Client has been tested against
rdesktop versions 1.4.1, 1.5.0, and 1.6.0.  rdesktop version 1.5.0 or
higher is required to connect to a Windows Vista or Windows Server
2008 Terminal Services desktop.

To use multiple monitors when connecting to desktops, the Xinerama
extension to the X Window System must be enabled with more than one
display defined. The system must also be running a window manager that
supports the _NET_WM_FULLSCREEN_MONITORS window manager protocol,
defined in freedesktop.org's Extended Window Manager Hints.

Recent releases of Metacity (2.26), Compiz (0.8), KWin for KDE (4.2),
and xfwm4 for xfce4 (4.6.0) support this protocol.

To use MMR, USB, and PCoIP, additional libraries and binaries are
available to partners.  As these are not available under an Open
Source license, we are unable to provide them via Google Code at this
time.


INSTALLATION
------------

VMware View Open Client is distributed in the following forms:
 * Binary tar gzip
 * Standard RPM
 * Novell SLETC Add-on RPM
 * Debian .deb package
 * Source tarball

To use the binary tar gzip, simply unpack the tarball:

$ tar zxf VMware-view-open-client-4.0.0-215529.tar.gz

To run, navigate to the 'bin' subdirectory and run directly from the
command line:

$ cd VMware-view-open-client-4.0.0-215529/bin
$ ./vmware-view

You can also copy files from the tarball into system directories:

# cp bin/* /usr/bin/
# cp -r share/locale /usr/share/
# cp -r doc /usr/share/doc/VMware-view-open-client

System directories for binaries, documentation, and locale files
can be defined manually when building from source; see the INSTALL file.

The regular RPM can be installed using 'rpm -i'. The Novell SLETC
Add-on RPM should be installed from an HTTP server using the Novell
Add-on Manager; see the SLETC documentation for full details.

The Debian package can be installed using 'dpkg -i'.

See the View Compatibility and System Requirements sections above for
information about compatibility.

To discover the command line arguments available, type

$ ./vmware-view --help

Note that, as with most Linux command-line programs, options must be
properly quoted to avoid being interpreted by the command shell.

General instructions for building from source can be found in the
INSTALL file of the open-source distribution.  You will need the following
packages installed, whose method will vary depending on your distribution:

Project          Version
-------------    -------
Glib             2.6.0
Gtk+             2.4.0
libxml2          2.6.0
libcurl          7.18.0
OpenSSL          0.9.8
Boost.Signals    1.34.1

If running configure fails, please check config.log for more details.


SMART CARD SUPPORT
------------------

To allow smart card authentication, install your PKCS#11 modules in
/usr/lib/vmware/view/pkcs11.  If the modules are already installed
elsewhere on the system, you can simply add symlinks instead of
copying the files.  The client will attempt to load all files in this
directory with a .so file name extension.

Consult your PKCS#11 module documentation for other possible
requirements, such as pcsc-lite, and the View Manager Administration
Guide for details on configuring smart card authentication for your
server.

To enable remote Windows login using smart cards, smart card support
must be enabled in your version of rdesktop; at minimum this requires
rdesktop 1.5.0 or later, and passing --enable-smartcard to rdesktop's
configure.  Consult the rdesktop documentation for more information.


ISSUES RESOLVED IN THIS RELEASE
-------------------------------

"gc" denotes issues reported on code.google.com.
"bz" denotes bugs filed in VMware's bugzilla.

Version 4.0.0 build 215529:

 * Resolved duplicate mnemonics on connection dialog (bz 490680)


ISSUES RESOLVED IN PREVIOUS RELEASES
------------------------------------

Version 4.0.0 build 207079 (RC 1):

*) Fix smart card login continuing to password screen (bz 461618)
*) Update RDC web link to point to version 2.0.1
*) Resize remote desktop to full screen when in full screen mode
   (bz 485605)
*) Update help text (bz 486415)
*) Update Admin Guide (bz 483500)
*) Launch desktop in full screen when using --fullscreen and previous
   invocation used a windowed session (bz 485605)
*) Do not send our "GMT Offset" to the broker since it leads to
   incorrect timezones on the remote desktop with 3.1 brokers
   (bz 479729, gc 42)
*) Support compiling against OpenSSL 1.0.0-beta3 (gc 45)
*) Fix a duplicate hotkey between "Connect" and "Cancel" buttons
   in smart card and PIN dialogs (bz 489093)

Version 4.0.0 build 201987 (Beta 2):

*) README.txt updated
*) Failed assertion after timeout (bz 475796)
*) Fixed compilation problems on OS X 10.6 Snow Leopard

Version 4.0.0 build 196715 (Beta 1):

*) New window size UI
*) Fixes to windowed mode keyboard focus
*) Improved View protocol support
*) Accepts -K option like rdesktop to not grab the keyboard
*) Unusual Time format for German and Japanese users on the Smart Card
   Cert screen (bz 390241)
*) When authenticating with smart cards, users will need to enter
   their PIN/password when connecting to the Windows desktop

Version 3.1.2 build 188088:

*) Added French translations
*) A new preference which no longer disables Metacity keybindings on
  SLED SP2 (set allowDisableMetacityKeybindingWorkaround to true in
  ~/.vmware/view-preferences)
*) Does not try to launch USB client if it's not installed

Version 3.1.0 build 169073:

*) Get "Gtk: gtk_main_quit: assertion `main_loops != NULL' failed" while
   using smart card authentication (bz 393737)
*) When entering a negative port value in the address bar of the broker
   screen the connect button is not disabled.  This also causes the "use
   secure connection" option to be enabled. (bz 367370)
*) Client will not build on Mac (bz 395525)
*) RDesktop does not launch on Mac (gc 23)
*) Terminates with NOT_IMPLEMENTED assertion on SLETC/SLED after
   running for a while, or when the tunnel address cannot be resolved
   (bz 409297)
*) /usr/share/doc/VMware-view-client/help directory will not be deleted
   when uninstalling from RPM (bz 365618)

Version 3.1.0 build 160969:

*) No sound with Linux client (bz 351637)
*) The desktop list cannot be refreshed (bz 291918)
*) No log collection script (bz 315390)
*) Debian package not available (bz 333101)
*) A useless error is shown if the tunnel is unreachable (bz 308760)

Version 2.1.1 build 153227:

*) Incorrect use of curl_easy_getinfo macro (gc 1)
*) Include vm_basic_asm_x86_64.h (gc 2)
*) Statically link against OpenSSL (gc 3)
*) Compilation fixes for building on Mac OS X (gc 4, gc 14, gc 15)
*) Include a windowed mode (gc 12, gc 17)

Version 2.1.1 build 144835:

*) Freeze when desktop selector window is resized (bz 333688)
*) Disclaimer text is not word-wrapped
*) Disclaimer buttons grow vertically when window is resized
*) Missing Linux Admin Guide (bz 348728)
*) Remote desktop does not get Alt+Enter or Control+Enter keypresses

Version 2.1.0:

*) No bundled End-User Documentation (bz 324486)
*) In one report, use of --fullscreen corrupted the rdesktop color palette
   (bz 323057, resolved as unable to duplicate)
*) VDM client hangs when guest firewall blocks RDP port (bz 325327)
*) Use 2.1 as version number (bz 327092)
*) Tunnel client assert when restarting connected desktop (bz 325330)
*) Window manager key bindings are not inhibited on Novell SLETC (bz 313232)


KNOWN ISSUES IN THIS RELEASE
----------------------------

*) In certain cases, an "access denied" error may be seen when trying to
   reconnect to a desktop that has been reset or powered off from the
   administrative UI (bz 330941)
*) The client may not time out when attempting to reach an unreachable
   broker (bz 322204, bz 325803)
*) The client may exit with SEGV (signal 11) after resetting a desktop
   (bz 407225)
*) When using smart cards, the view client may hang when quitting
   (bz 407231)
*) 'Retry' and 'Cancel' buttons don't work after network interruption
   (bz 474562)

TROUBLESHOOTING
---------------

To collect client log files, run:

$ ./vmware-view-log-collector

Use the --help option for more details. Please include the file
'view-log.tar.gz' created by the script with any issue reported to VMware.


SUPPORT
-------

At the time of this release, official builds of VMware View Client for
Linux are only available through certified partners. Support is
available through them. A list of certified partners can be found on
vmware.com.

Official builds have a blue banner, with the text, "VMware View,"
while versions based on the source release have an orange banner, with
the text, "VMware View Open Client."

Informal support for the source distribution may be found at:

http://code.google.com/p/vmware-view-open-client/


OPEN SOURCE
-----------

VMware View Open Client includes software which may be covered
under one or more open source agreements. For more information please
see the open_source_licenses.txt file located in the doc directory.


FEATURES
--------

*) Password authentication
*) SSL support
*) Secure tunnel
*) RSA authentication
*) View Broker version detection
*) Notification of forced disconnect and timeout
*) Full command-line interface
*) Confirmations, warnings, errors
*) Fullscreen mode
*) Disclaimer dialog
*) Password change dialog
*) Desktop reset (right-click entry in desktop selector)
*) Generic RPM package
*) Novell SLETC Add-on RPM package
*) Windowed mode
*) USB redirection
*) Multi-monitor sessions

The following features of the View Client for Windows 4.0 are not
implemented in this release.

*) Multiple desktop sessions
*) Fullscreen toolbar
*) Log in as current user
