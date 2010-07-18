# -*- Makefile -*-
################################################################################
# Copyright 2009 VMware, Inc.  All rights reserved.
#
# This file is part of VMware View Open Client.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation version 2.1 and no later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
################################################################################

if VIEW_COCOA
vmware_view_SOURCES += cocoa/app.hh
vmware_view_SOURCES += cocoa/app.m
vmware_view_SOURCES += cocoa/brokerAdapter.hh
vmware_view_SOURCES += cocoa/brokerAdapter.m
vmware_view_SOURCES += cocoa/cdkAppController.h
vmware_view_SOURCES += cocoa/cdkAppController.m
vmware_view_SOURCES += cocoa/cdkBroker.h
vmware_view_SOURCES += cocoa/cdkBroker.m
vmware_view_SOURCES += cocoa/cdkBrokerAddress.h
vmware_view_SOURCES += cocoa/cdkBrokerAddress.m
vmware_view_SOURCES += cocoa/cdkBrokerViewController.h
vmware_view_SOURCES += cocoa/cdkBrokerViewController.m
vmware_view_SOURCES += cocoa/cdkChangePinCreds.h
vmware_view_SOURCES += cocoa/cdkChangePinCredsViewController.h
vmware_view_SOURCES += cocoa/cdkChangePinCredsViewController.m
vmware_view_SOURCES += cocoa/cdkChangeWinCreds.h
vmware_view_SOURCES += cocoa/cdkChangeWinCredsViewController.h
vmware_view_SOURCES += cocoa/cdkChangeWinCredsViewController.m
vmware_view_SOURCES += cocoa/cdkConfirmPinCreds.h
vmware_view_SOURCES += cocoa/cdkConfirmPinCredsViewController.h
vmware_view_SOURCES += cocoa/cdkConfirmPinCredsViewController.m
vmware_view_SOURCES += cocoa/cdkCreds.h
vmware_view_SOURCES += cocoa/cdkCreds.m
vmware_view_SOURCES += cocoa/cdkDebugAssert.h
vmware_view_SOURCES += cocoa/cdkDebugAssert.c
vmware_view_SOURCES += cocoa/cdkDesktop.h
vmware_view_SOURCES += cocoa/cdkDesktop.m
vmware_view_SOURCES += cocoa/cdkDesktopCell.h
vmware_view_SOURCES += cocoa/cdkDesktopCell.m
vmware_view_SOURCES += cocoa/cdkDesktopFormatter.h
vmware_view_SOURCES += cocoa/cdkDesktopFormatter.m
vmware_view_SOURCES += cocoa/cdkDesktopSize.h
vmware_view_SOURCES += cocoa/cdkDesktopSize.m
vmware_view_SOURCES += cocoa/cdkDesktopSizesWindowController.h
vmware_view_SOURCES += cocoa/cdkDesktopSizesWindowController.m
vmware_view_SOURCES += cocoa/cdkDesktopsViewController.h
vmware_view_SOURCES += cocoa/cdkDesktopsViewController.m
vmware_view_SOURCES += cocoa/cdkDisclaimer.h
vmware_view_SOURCES += cocoa/cdkDisclaimer.m
vmware_view_SOURCES += cocoa/cdkDisclaimerViewController.h
vmware_view_SOURCES += cocoa/cdkDisclaimerViewController.m
vmware_view_SOURCES += cocoa/cdkKeychain.h
vmware_view_SOURCES += cocoa/cdkKeychain.m
vmware_view_SOURCES += cocoa/cdkPasscodeCreds.h
vmware_view_SOURCES += cocoa/cdkPasscodeCredsViewController.h
vmware_view_SOURCES += cocoa/cdkPasscodeCredsViewController.m
vmware_view_SOURCES += cocoa/cdkPrefs.h
vmware_view_SOURCES += cocoa/cdkPrefs.m
vmware_view_SOURCES += cocoa/cdkProcHelper.h
vmware_view_SOURCES += cocoa/cdkProcHelper.m
vmware_view_SOURCES += cocoa/cdkRdc.h
vmware_view_SOURCES += cocoa/cdkRdc.m
vmware_view_SOURCES += cocoa/cdkString.h
vmware_view_SOURCES += cocoa/cdkString.m
vmware_view_SOURCES += cocoa/cdkTokencodeCreds.h
vmware_view_SOURCES += cocoa/cdkTokencodeCredsViewController.h
vmware_view_SOURCES += cocoa/cdkTokencodeCredsViewController.m
vmware_view_SOURCES += cocoa/cdkViewController.h
vmware_view_SOURCES += cocoa/cdkViewController.m
vmware_view_SOURCES += cocoa/cdkWaitingViewController.h
vmware_view_SOURCES += cocoa/cdkWaitingViewController.m
vmware_view_SOURCES += cocoa/cdkWinCreds.h
vmware_view_SOURCES += cocoa/cdkWinCredsViewController.h
vmware_view_SOURCES += cocoa/cdkWinCredsViewController.m
vmware_view_SOURCES += cocoa/cdkWindowController.h
vmware_view_SOURCES += cocoa/cdkWindowController.m
vmware_view_SOURCES += cocoa/main.m
endif # VIEW_COCOA

nibs :=
nibs += BrokerView.nib
nibs += ChangePinCredsView.nib
nibs += ChangeWinCredsView.nib
nibs += ConfirmPinCredsView.nib
nibs += DesktopSizesWindow.nib
nibs += DesktopsView.nib
nibs += DisclaimerView.nib
nibs += MainMenu.nib
nibs += MainWindow.nib
nibs += PasscodeCredsView.nib
nibs += TokencodeCredsView.nib
nibs += WaitingView.nib
nibs += WinCredsView.nib

nib_files =
nib_files += $(addsuffix /designable.nib,$(nibs))
nib_files += $(addsuffix /keyedobjects.nib,$(nibs))

base_lang_files :=
base_lang_files += $(nib_files)
base_lang_files += Credits.html
base_lang_files += InfoPlist.strings

base_lang_dirs :=
base_lang_dirs += $(nibs)

lang_files :=
lang_files += $(addprefix English.lproj/,$(base_lang_files))
lang_files += $(addprefix French.lproj/,$(base_lang_files))
lang_files += $(addprefix German.lproj/,$(base_lang_files))
lang_files += $(addprefix Japanese.lproj/,$(base_lang_files))
lang_files += $(addprefix zh-Hans.lproj/,$(base_lang_files))

lang_dirs :=
lang_dirs += $(addprefix English.lproj/,$(base_lang_dirs))
lang_dirs += $(addprefix French.lproj/,$(base_lang_dirs))
lang_dirs += $(addprefix German.lproj/,$(base_lang_dirs))
lang_dirs += $(addprefix Japanese.lproj/,$(base_lang_dirs))
lang_dirs += $(addprefix zh-Hans.lproj/,$(base_lang_dirs))

help_zips :=
help_zips += $(foreach lang,en $(ALL_LINGUAS),cocoa/viewclient_mac_help_$(lang).zip)

dist_noinst_DATA += $(addprefix cocoa/,$(lang_files))
dist_noinst_DATA += fix-toolchain-paths.sh.in
dist_noinst_DATA += $(ICONS_DIR)/view.icns
dist_noinst_DATA += $(ICONS_DIR)/view_client_banner.png
dist_noinst_DATA += cocoa/vmware-view.rdp
dist_noinst_DATA += cocoa/rdc.webloc
dist_noinst_DATA += $(help_zips)

dist_noinst_SCRIPTS += cocoa/pkg-dmg

desktop_icons :=
desktop_icons += desktop_checkin_32x.png
desktop_icons += desktop_checkin_pause32x.png
desktop_icons += desktop_checkout_32x.png
desktop_icons += desktop_checkout_pause32x.png
desktop_icons += desktop_local32xdisabled.png
desktop_icons += desktop_local32x.png
desktop_icons += desktop_local_rollback_32x.png
desktop_icons += desktop_remote32x.png
desktop_icons += desktop_remote32x_disabled.png

appname := "$(PRODUCT_VIEW_CLIENT_NAME)"
appdir := $(appname).app
appdmg := $(subst $(space),$(dash),$(appname))-$(VERSION).dmg

if VIEW_COCOA
ALL += app
endif
app: vmware-view vmware-view-tunnel Info.plist fix-toolchain-paths.sh
	rm -rf $(appdir) locale.tmp
	$(MKDIR_P) $(appdir)/Contents/MacOS
	$(MKDIR_P) $(appdir)/Contents/Frameworks
	@for i in $(lang_dirs) ; do \
		echo $(MKDIR_P) $(appdir)/Contents/Resources/$$i ; \
		$(MKDIR_P) $(appdir)/Contents/Resources/$$i ; \
	done
	$(INSTALL_DATA) Info.plist $(appdir)/Contents
ifeq ($(PUBLISH_DIR),)
	$(INSTALL_PROGRAM) vmware-view $(appdir)/Contents/MacOS
	$(INSTALL_PROGRAM) vmware-view-tunnel $(appdir)/Contents/MacOS
@ELSE@
	STRIPPROG='$(STRIP)' $(INSTALL_STRIP_PROGRAM) vmware-view \
		$(appdir)/Contents/MacOS
	STRIPPROG='$(STRIP)' $(INSTALL_STRIP_PROGRAM) vmware-view-tunnel \
		$(appdir)/Contents/MacOS
@ENDIF@
	$(INSTALL_DATA) $(ICONS_DIR)/view.icns \
		$(appdir)/Contents/Resources
	$(INSTALL_DATA) $(ICONS_DIR)/view_client_banner.png \
		$(appdir)/Contents/Resources
	@for i in $(desktop_icons) ; do \
		echo $(INSTALL_DATA) $(top_srcdir)icons/$$i \
			$(appdir)/Contents/Resources ; \
		$(INSTALL_DATA) $(top_srcdir)/icons/$$i \
			$(appdir)/Contents/Resources ; \
	done
	$(INSTALL_DATA) $(srcdir)/cocoa/vmware-view.rdp \
		$(appdir)/Contents/Resources
	@for i in $(lang_files) ; do \
		echo $(INSTALL_DATA) $(srcdir)/cocoa/$$i \
			$(appdir)/Contents/Resources/$$i ; \
		$(INSTALL_DATA) $(srcdir)/cocoa/$$i \
			$(appdir)/Contents/Resources/$$i ; \
	done
	$(MAKE) -C po $(AM_MAKEFLAGS) install itlocaledir=../locale.tmp
	mv locale.tmp $(appdir)/Contents/Resources/locale
# this is crazy! we use $UNZIP to allow someone to specify the path to
# unzip, but unzip uses that for its options! so if it's set, it'll
# try to unzip itself. clearing $UNZIP before running unzip fixes
# this, but will never get back the hour of my life i just lost
# tracking this down.
	@for i in $(help_zips) ; do \
		echo UNZIP= $(UNZIP) $(srcdir)/$$i -d $(appdir)/Contents/Resources ; \
		UNZIP= $(UNZIP) $(srcdir)/$$i -d $(appdir)/Contents/Resources ; \
	done
	/bin/bash ./fix-toolchain-paths.sh $(appdir)/Contents/MacOS/vmware-view \
		$(appdir)/Contents/Frameworks
	/bin/bash ./fix-toolchain-paths.sh $(appdir)/Contents/MacOS/vmware-view-tunnel \
		$(appdir)/Contents/Frameworks

stagedir := $(top_builddir)/dmg-stage

dmg: app
	-rm -rf $(stagedir)
	mkdir -p $(stagedir)/$(appname)
	cp -r $(top_builddir)/$(appdir) $(stagedir)/$(appname)
	cp $(top_srcdir)/cocoa/rdc.webloc $(stagedir)/$(appname)/"Download Microsoft RDC.webloc"
	cp doc/open_source_licenses.txt $(stagedir)/$(appname)
	cp $(ICONS_DIR)/view.icns $(stagedir)
ifeq ($(findstring darwin,$(build_os)),darwin)
	perl $(top_srcdir)/cocoa/pkg-dmg $(dmgeula) \
		--source $(stagedir)/$(appname) \
		--target $(top_builddir)/$(appdmg) \
		--icon $(stagedir)/view.icns
if VIEW_EULAS
	$(HDIUTIL) unflatten $(top_builddir)/$(appdmg)
	@for lang in en $(ALL_LINGUAS) ; do \
		for dir in "$(srcdir)/doc" "$(builddir)/doc" ; do \
			eula=$$dir/$(PACKAGING_NAME)-EULA-$$lang.rtf ; \
			if test -f $$eula ; then \
				echo $(ADD_LICENSE) "$(appdmg)" $$lang $$eula ; \
				$(ADD_LICENSE) "$(appdmg)" $$lang $$eula ; \
				break ; \
			fi \
		done \
	done
	$(HDIUTIL) flatten $(top_builddir)/$(appdmg)
endif
@ELSE@
	cp $(top_srcdir)/cocoa/pkg-dmg $(stagedir)
@ENDIF@
