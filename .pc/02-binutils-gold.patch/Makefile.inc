# -*- Makefile -*-
################################################################################
# Copyright 2008 VMware, Inc.  All rights reserved.
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

if VIEW_GTK
bin_PROGRAMS += vmware-view
dist_bin_SCRIPTS += vmware-view-log-collector
endif
if VIEW_COCOA
noinst_PROGRAMS += vmware-view
endif
noinst_PROGRAMS += testUrl

nodist_vmware_view_SOURCES :=
vmware_view_SOURCES :=
# Files that are in all versions
vmware_view_SOURCES += baseApp.cc
vmware_view_SOURCES += baseApp.hh
vmware_view_SOURCES += baseXml.cc
vmware_view_SOURCES += baseXml.hh
vmware_view_SOURCES += broker.cc
vmware_view_SOURCES += broker.hh
vmware_view_SOURCES += brokerXml.cc
vmware_view_SOURCES += brokerXml.hh
vmware_view_SOURCES += cdkErrors.h
vmware_view_SOURCES += cdkUrl.c
vmware_view_SOURCES += cdkUrl.h
vmware_view_SOURCES += desktop.cc
vmware_view_SOURCES += desktop.hh
vmware_view_SOURCES += procHelper.cc
vmware_view_SOURCES += procHelper.hh
vmware_view_SOURCES += protocols.cc
vmware_view_SOURCES += protocols.hh
vmware_view_SOURCES += restartMonitor.cc
vmware_view_SOURCES += restartMonitor.hh
vmware_view_SOURCES += stubs.c
vmware_view_SOURCES += tunnel.cc
vmware_view_SOURCES += tunnel.hh
vmware_view_SOURCES += usb.cc
vmware_view_SOURCES += usb.hh
vmware_view_SOURCES += util.cc
vmware_view_SOURCES += util.hh

noinst_LIBRARIES += libCdk.a
libCdk_a_SOURCES :=
libCdk_a_SOURCES += cdkProxy.h
if VIEW_COCOA
libCdk_a_SOURCES += cdkProxyDarwin.c
else
libCdk_a_SOURCES += cdkProxy.c
endif
libCdk_a_SOURCES += cdkUrl.c
libCdk_a_SOURCES += cdkUrl.h

libCdk_a_CPPFLAGS =
libCdk_a_CPPFLAGS += $(AM_CPPFLAGS)
libCdk_a_CPPFLAGS += $(GLIB_CFLAGS)
libCdk_a_CPPFLAGS += $(ICU_CPPFLAGS)

dist_noinst_DATA += icudata/NamePrepProfile.txt
dist_noinst_DATA += icudata/NormalizationCorrections.txt
dist_noinst_DATA += icudata/README

dist_patch_DATA += rdesktop-1.6.0-win7scard.patch

dist_noinst_HEADERS += extraTranslations.hh

if STATIC_ICU
nodist_vmware_view_SOURCES += $(ICUDATA_ENTRY_POINT)l_dat.s

uidna.spp: icudata/NamePrepProfile.txt icudata/NormalizationCorrections.txt
	$(GENSPREP) -b uidna -s $(srcdir)/icudata -n $(srcdir)/icudata \
		-u 3.2.0 -k NamePrepProfile.txt

# l suffix means little endian
$(ICUDATA_ENTRY_POINT)l.dat: uidna.spp
	echo $^ > tmp.lst
	$(ICUPKG) -tl -c -a tmp.lst new $@
	-rm -f tmp.lst

$(ICUDATA_ENTRY_POINT)l_dat.s: $(ICUDATA_ENTRY_POINT)l.dat
if VIEW_WIN32
	$(GENCCODE) -a $(GENCCODE_ASM_TARGET) -n $(ICUDATA_ENTRY_POINT)l -e $(ICUDATA_ENTRY_POINT) -f $@.tmp $<
	sed -e 's/-stack,"",@progbits/,""/' -e '/\.type icudt38_dat,@object/d' $@.tmp.s > $@
	-rm -f $@.tmp.s
else
	$(GENCCODE) -a $(GENCCODE_ASM_TARGET) -n $(ICUDATA_ENTRY_POINT)l -e $(ICUDATA_ENTRY_POINT) $<
endif

uidna_spp.c: uidna.spp
	$(GENCCODE) -n $(ICUDATA_ENTRY_POINT)l $<
endif # STATIC_ICU

dist_noinst_DATA += icons/desktop_checkin_32x.png
dist_noinst_DATA += icons/desktop_checkin_pause32x.png
dist_noinst_DATA += icons/desktop_checkout_32x.png
dist_noinst_DATA += icons/desktop_checkout_pause32x.png
dist_noinst_DATA += icons/desktop_local32x.png
dist_noinst_DATA += icons/desktop_local32xdisabled.png
dist_noinst_DATA += icons/desktop_local_rollback_32x.png
dist_noinst_DATA += icons/desktop_remote32x.png
dist_noinst_DATA += icons/desktop_remote32x_disabled.png
dist_noinst_DATA += icons/list_button_hover.png
dist_noinst_DATA += icons/list_button_normal.png
dist_noinst_DATA += icons/list_button_open.png
dist_noinst_DATA += icons/open/view_16x.png
dist_noinst_DATA += icons/open/view_32x.png
dist_noinst_DATA += icons/open/view_48x.png
dist_noinst_DATA += icons/open/view_client_banner.png
dist_noinst_DATA += icons/spinner_anim.png

window_icons :=
window_icons += icons/spinner_anim.h
window_icons += icons/view_16x.h
window_icons += icons/view_32x.h
window_icons += icons/view_48x.h
window_icons += icons/view_client_banner.h
if VIEW_PCOIP_BANNER
window_icons += icons/view_client_banner_pcoip.h
endif

gtk/vmware_view-window.$(OBJEXT): $(window_icons)

gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_checkin_32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_checkin_pause32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_checkout_32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_checkout_pause32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_local32xdisabled.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_local32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_local_rollback_32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_remote32x.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/desktop_remote32x_disabled.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/list_button_hover.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/list_button_normal.h
gtk/vmware_view-desktopSelectDlg.$(OBJEXT): icons/list_button_open.h

vpath %.png $(ICONS_DIR):$(top_srcdir)/icons

icons/%.h: %.png
	-rm -f $@.tmp $@
	-$(MKDIR_P) $(@D)
	$(GDK_PIXBUF_CSOURCE) --name $(@F:.h=) $< > $@.tmp
	mv $@.tmp $@

vmware_view_CPPFLAGS =
vmware_view_CPPFLAGS += $(AM_CPPFLAGS)
vmware_view_CPPFLAGS += -DCRYPTOKI_COMPAT
vmware_view_CPPFLAGS += -DLIBDIR="\"$(libdir)\""
vmware_view_CPPFLAGS += -DBINDIR="\"$(bindir)\""
vmware_view_CPPFLAGS += -DLOCALEDIR="\"$(localedir)\""
vmware_view_CPPFLAGS += -DHELPDIR="\"$(helpdir)\""
vmware_view_CPPFLAGS += -DDEBHELPDIR="\"$(datarootdir)/doc/$(DEB_PACKAGING_NAME)/help\""
vmware_view_CPPFLAGS += -I$(top_srcdir)/lib/libp11/include
vmware_view_CPPFLAGS += -I$(top_srcdir)/lib/gtm/include
vmware_view_CPPFLAGS += $(BOOST_CPPFLAGS)
if VIEW_COCOA
vmware_view_CPPFLAGS += $(GLIB_CFLAGS)
vmware_view_CPPFLAGS += -I$(srcdir)/cocoa
endif
if VIEW_GTK
vmware_view_CPPFLAGS += $(GTK_CFLAGS)
vmware_view_CPPFLAGS += -I$(srcdir)/gtk
endif
vmware_view_CPPFLAGS += $(ICU_CPPFLAGS)
vmware_view_CPPFLAGS += $(SSL_CFLAGS)
vmware_view_CPPFLAGS += $(XML_CFLAGS)

vmware_view_OBJCFLAGS =
# Automake does not support .mm files, so we need to turn
# Objective-C++ on manually.
vmware_view_OBJCFLAGS += -x objective-c++

vmware_view_LDADD :=
vmware_view_LDADD += libFile.a
vmware_view_LDADD += libPanicDefault.a
vmware_view_LDADD += libPanic.a
vmware_view_LDADD += libString.a
vmware_view_LDADD += libStubs.a
vmware_view_LDADD += libLog.a
vmware_view_LDADD += libUser.a
vmware_view_LDADD += libBasicHttp.a
vmware_view_LDADD += libNothread.a
vmware_view_LDADD += libPoll.a
vmware_view_LDADD += libPollDefault.a
if VIEW_GTK
vmware_view_LDADD += libPollGtk.a
vmware_view_LDADD += libGtm.a
endif
if VIEW_COCOA
vmware_view_LDADD += libDui.a
endif
vmware_view_LDADD += libProductState.a
vmware_view_LDADD += libSig.a
vmware_view_LDADD += libDict.a
vmware_view_LDADD += libMisc.a
vmware_view_LDADD += libErr.a
vmware_view_LDADD += libUnicode.a
vmware_view_LDADD += libCdk.a
if VIEW_COCOA
vmware_view_LDADD += $(GLIB_LIBS)
endif
if VIEW_GTK
vmware_view_LDADD += $(GTK_LIBS)
endif
vmware_view_LDADD += $(XML_LIBS)
vmware_view_LDADD += $(CURL_LIBS)
vmware_view_LDADD += $(SSL_LIBS)
vmware_view_LDADD += $(BOOST_LDFLAGS) $(BOOST_SIGNALS_LIB)
vmware_view_LDADD += $(ICU_LIBS)
vmware_view_LDADD += $(VIEW_FRAMEWORKS)

testUrl_SOURCES :=
testUrl_SOURCES += testUrl.cc
nodist_testUrl_SOURCES :=
if STATIC_ICU
nodist_testUrl_SOURCES += $(ICUDATA_ENTRY_POINT)l_dat.s
endif

testUrl_CPPFLAGS =
testUrl_CPPFLAGS += $(AM_CPPFLAGS)
testUrl_CPPFLAGS += $(GLIB_CFLAGS)
testUrl_CPPFLAGS += $(ICU_CPPFLAGS)

testUrl_LDADD :=
testUrl_LDADD += libCdk.a
testUrl_LDADD += $(GLIB_LIBS)
testUrl_LDADD += $(ICU_LIBS)
testUrl_LDADD += $(TUNNEL_FRAMEWORKS)

dist_noinst_DATA += intltool-extract.in
dist_noinst_DATA += intltool-merge.in
dist_noinst_DATA += intltool-update.in

desktop_in_files += vmware-view.desktop.in

dist_noinst_DATA += INSTALL

dist_doc_DATA += README.txt

pixmaps_DATA += vmware-view.png

vmware-view.png: $(ICONS_DIR)/view_48x.png
	-rm -f $@
	cp $^ $@

CLEANFILES += vmware-view.png

# framework/cdk/po/Makefile.in.in wants mkinstalldirs.
installdirs-recursive: $(top_srcdir)/mkinstalldirs

$(top_srcdir)/mkinstalldirs: $(top_srcdir)/config/mkinstalldirs
	cp -f $^ $@

dist_noinst_DATA += mkinstalldirs

DEB_STAGE_ROOT = $(shell pwd)/deb-stage

deb-stage: doc/changelog.Debian all-am
	rm -rf $(DEB_STAGE_ROOT)
	$(MAKE) $(AM_MAKEFLAGS) install-strip DESTDIR=$(DEB_STAGE_ROOT) \
		docdir=${datarootdir}/doc/$(DEB_PACKAGING_NAME)
	$(MKDIR_P) $(DEB_STAGE_ROOT)/usr/share/doc/$(DEB_PACKAGING_NAME)
	$(INSTALL_DATA) doc/changelog.Debian \
		$(DEB_STAGE_ROOT)${datarootdir}/doc/$(DEB_PACKAGING_NAME)
	$(INSTALL_DATA) -T $(srcdir)/doc/copyright.Debian \
		$(DEB_STAGE_ROOT)${datarootdir}/doc/$(DEB_PACKAGING_NAME)/copyright
	$(GZIP) -9 $(DEB_STAGE_ROOT)$(mandir)/man*/* \
		$(DEB_STAGE_ROOT)${datarootdir}/doc/$(DEB_PACKAGING_NAME)/changelog*

debs: VMware-view-open-client.control deb-stage
	$(MKDIR_P) $(DEB_STAGE_ROOT)/DEBIAN
	$(INSTALL_DATA) -T $< $(DEB_STAGE_ROOT)/DEBIAN/control
	$(FAKEROOT) $(DPKG_DEB) --build $(DEB_STAGE_ROOT)
	mv $(DEB_STAGE_ROOT).deb $(PACKAGING_NAME)_$(VERSION)_$(DEB_ARCH).deb
	-rm -rf $(DEB_STAGE_ROOT)

rpm-stage: all-am
	rm -rf $(RPM_STAGE_ROOT)
	$(MAKE) $(AM_MAKEFLAGS) install-strip DESTDIR=$(RPM_STAGE_ROOT)
	$(GZIP) -9 $(RPM_STAGE_ROOT)$(mandir)/man*/*

$(PACKAGING_NAME)-lite.squash: rpm-stage
	$(MKSQUASHFS) $(RPM_STAGE_ROOT) $@ -all-root -noappend
	$(MKDIR_P) $(RPM_STAGE_ROOT)/var/lib/addons
	$(INSTALL_DATA) $@ $(RPM_STAGE_ROOT)/var/lib/addons/$@

ifneq ($(MKSQUASHFS),)
rpms: $(PACKAGING_NAME)-lite.squash
@ENDIF@ # MKSQUASHFS

rpms: VMware-view-open-client.spec rpm-stage
	rm -rf $(RPM_OUTPUT_DIR)
	$(MKDIR_P) $(RPM_OUTPUT_DIR)/BUILD
	$(MKDIR_P) $(RPM_OUTPUT_DIR)/RPMS/$(RPM_ARCH)
	$(RPMBUILD) -bb --target=$(RPM_ARCH) VMware-view-open-client.spec \
		--define "_use_internal_dependency_generator 0" \
		--define "__spec_install_post %{nil}" \
		--define "__find_requires %{nil}" \
		--define "__find_provides %{nil}" \
		--define "_topdir $(RPM_OUTPUT_DIR)"
	cp $(RPM_OUTPUT_DIR)/RPMS/$(RPM_ARCH)/* .
	-rm -rf $(RPM_OUTPUT_DIR) $(RPM_STAGE_ROOT)

TAR_STAGE_DIR := $(PACKAGING_NAME)-$(VERSION).$(RPM_ARCH)

# Make the locale stuff in the tar stage dir. Note the .. since we're entering
# po/ and want the stage dir to be in the directory above it.
bindist: all-am
	rm -rf $(TAR_STAGE_DIR)
	$(MKDIR_P) $(TAR_STAGE_DIR)/bin
	$(MKDIR_P) $(TAR_STAGE_DIR)/doc/help/de
	$(MKDIR_P) $(TAR_STAGE_DIR)/doc/help/en
	$(MKDIR_P) $(TAR_STAGE_DIR)/doc/help/fr
	$(MKDIR_P) $(TAR_STAGE_DIR)/doc/help/ja
	$(MKDIR_P) $(TAR_STAGE_DIR)/doc/help/zh_CN
	$(MKDIR_P) $(TAR_STAGE_DIR)/patches
	STRIPPROG='$(STRIP)' $(INSTALL_STRIP_PROGRAM) vmware-view$(EXEEXT) \
		$(TAR_STAGE_DIR)/bin
	STRIPPROG='$(STRIP)' $(INSTALL_STRIP_PROGRAM) vmware-view-tunnel$(EXEEXT) \
		$(TAR_STAGE_DIR)/bin
	$(INSTALL_DATA) README.txt $(TAR_STAGE_DIR)
	$(INSTALL_DATA) doc/open_source_licenses.txt \
		$(TAR_STAGE_DIR)/doc
if VIEW_EULAS
	@for lang in en $(ALL_LINGUAS) ; do \
		echo $(INSTALL_DATA) $(srcdir)/doc/$(PACKAGING_NAME)-EULA-$$lang.txt \
			$(TAR_STAGE_DIR)/doc "||" \
		$(INSTALL_DATA) $(builddir)/doc/$(PACKAGING_NAME)-EULA-$$lang.txt \
			$(TAR_STAGE_DIR)/doc ; \
		$(INSTALL_DATA) $(srcdir)/doc/$(PACKAGING_NAME)-EULA-$$lang.txt \
			$(TAR_STAGE_DIR)/doc || \
		$(INSTALL_DATA) $(builddir)/doc/$(PACKAGING_NAME)-EULA-$$lang.txt \
			$(TAR_STAGE_DIR)/doc ; \
	done
endif
	@for lang in en $(ALL_LINGUAS) ; do \
		if test -e $(srcdir)/doc/help/$$lang ; then \
			echo $(INSTALL_DATA) $(srcdir)/doc/help/$$lang/*.txt \
				$(TAR_STAGE_DIR)/doc/help/$$lang ; \
			$(INSTALL_DATA) $(srcdir)/doc/help/$$lang/*.txt \
				$(TAR_STAGE_DIR)/doc/help/$$lang ; \
		fi ; \
	done
	$(INSTALL_DATA) $(srcdir)/doc/View_Client_Admin_Guide-en.pdf \
		$(TAR_STAGE_DIR)/doc
	$(INSTALL_SCRIPT) $(srcdir)/vmware-view-log-collector \
		$(TAR_STAGE_DIR)/bin
	$(INSTALL_DATA) $(srcdir)/rdesktop-1.6.0-win7scard.patch \
		$(TAR_STAGE_DIR)/patches
	$(MAKE) -C po $(AM_MAKEFLAGS) install itlocaledir=../$(TAR_STAGE_DIR)/share/locale
	tardir=$(TAR_STAGE_DIR) && $(am__tar) | GZIP=$(GZIP_ENV) $(GZIP) -c \
		>$(TAR_STAGE_DIR).tar.gz
	-rm -rf $(TAR_STAGE_DIR)

DELIVERABLES :=

if VIEW_GTK
if VIEW_WIN32
DELIVERABLES += $(bin_PROGRAMS)
else
DELIVERABLES += bindist

ifneq ($(RPMBUILD),)
DELIVERABLES += rpms
@ENDIF@

ifneq ($(DPKG_DEB),)
DELIVERABLES += debs
@ENDIF@
endif # VIEW_WIN32
endif # VIEW_GTK

if VIEW_COCOA
DELIVERABLES += dmg
endif #VIEW_COCOA

deliverables: $(DELIVERABLES)
ifneq ($(PUBLISH_DIR),)
	$(MKDIR_P) $(PUBLISH_DIR)
if VIEW_GTK
if VIEW_WIN32
# nothing to do for win32/mingw32 as yet.
else
	$(INSTALL_DATA) $(TAR_STAGE_DIR).tar.gz $(PUBLISH_DIR)
ifneq ($(RPMBUILD),)
	$(INSTALL_DATA) \
		$(PACKAGING_NAME)-$(PACKAGE_VERSION).$(RPM_ARCH).rpm \
		$(PUBLISH_DIR)
ifneq ($(MKSQUASHFS),)
	$(INSTALL_DATA) \
		$(PACKAGING_NAME)-lite-$(PACKAGE_VERSION).$(RPM_ARCH).rpm \
		$(PUBLISH_DIR)
@ENDIF@ # MKSQUASHFS
@ENDIF@ # RPMBUILD
ifneq ($(DPKG_DEB),)
	$(INSTALL_DATA) $(PACKAGING_NAME)_$(VERSION)_$(DEB_ARCH).deb \
		$(PUBLISH_DIR)
@ENDIF@ # DPKG_DEB
endif # VIEW_WIN32
endif # VIEW_GTK
if VIEW_COCOA
ifeq ($(findstring darwin,$(build_os)),darwin)
	$(INSTALL_DATA) $(appdmg) $(PUBLISH_DIR)
@ENDIF@
endif # VIEW_COCOA
@ENDIF@ # PUBLISH_DIR
