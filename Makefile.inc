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

bin_PROGRAMS += vmware-view

dist_bin_SCRIPTS += vmware-view-log-collector

nodist_vmware_view_SOURCES :=
vmware_view_SOURCES :=
vmware_view_SOURCES += app.cc
vmware_view_SOURCES += app.hh
vmware_view_SOURCES += broker.cc
vmware_view_SOURCES += broker.hh
vmware_view_SOURCES += brokerDlg.cc
vmware_view_SOURCES += brokerDlg.hh
vmware_view_SOURCES += brokerXml.cc
vmware_view_SOURCES += brokerXml.hh
vmware_view_SOURCES += cryptoki.cc
vmware_view_SOURCES += cryptoki.hh
vmware_view_SOURCES += desktop.cc
vmware_view_SOURCES += desktop.hh
vmware_view_SOURCES += desktopSelectDlg.cc
vmware_view_SOURCES += desktopSelectDlg.hh
vmware_view_SOURCES += disclaimerDlg.cc
vmware_view_SOURCES += disclaimerDlg.hh
vmware_view_SOURCES += dlg.cc
vmware_view_SOURCES += dlg.hh
vmware_view_SOURCES += helpDlg.cc
vmware_view_SOURCES += helpDlg.hh
vmware_view_SOURCES += loginDlg.cc
vmware_view_SOURCES += loginDlg.hh
vmware_view_SOURCES += main.cc
vmware_view_SOURCES += passwordDlg.cc
vmware_view_SOURCES += passwordDlg.hh
vmware_view_SOURCES += prefs.cc
vmware_view_SOURCES += prefs.hh
vmware_view_SOURCES += procHelper.cc
vmware_view_SOURCES += procHelper.hh
vmware_view_SOURCES += rdesktop.cc
vmware_view_SOURCES += rdesktop.hh
vmware_view_SOURCES += restartMonitor.cc
vmware_view_SOURCES += restartMonitor.hh
vmware_view_SOURCES += scCertDetailsDlg.cc
vmware_view_SOURCES += scCertDetailsDlg.hh
vmware_view_SOURCES += scCertDlg.cc
vmware_view_SOURCES += scCertDlg.hh
vmware_view_SOURCES += scInsertPromptDlg.cc
vmware_view_SOURCES += scInsertPromptDlg.hh
vmware_view_SOURCES += scPinDlg.cc
vmware_view_SOURCES += scPinDlg.hh
vmware_view_SOURCES += securIDDlg.cc
vmware_view_SOURCES += securIDDlg.hh
vmware_view_SOURCES += stubs.c
vmware_view_SOURCES += transitionDlg.cc
vmware_view_SOURCES += transitionDlg.hh
vmware_view_SOURCES += tunnel.cc
vmware_view_SOURCES += tunnel.hh
vmware_view_SOURCES += usb.cc
vmware_view_SOURCES += usb.hh
vmware_view_SOURCES += util.cc
vmware_view_SOURCES += util.hh

dist_noinst_DATA += icudata/NamePrepProfile.txt
dist_noinst_DATA += icudata/NormalizationCorrections.txt
dist_noinst_DATA += icudata/README

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
	$(GENCCODE) -a gcc -n $(ICUDATA_ENTRY_POINT)l -e $(ICUDATA_ENTRY_POINT) $<

uidna_spp.c: uidna.spp
	$(GENCCODE) -n $(ICUDATA_ENTRY_POINT)l $<
endif

vmware_view_SOURCES += icons/desktop_remote32x.h
vmware_view_SOURCES += icons/spinner_anim.h
vmware_view_SOURCES += icons/open/view_16x.h
vmware_view_SOURCES += icons/open/view_32x.h
vmware_view_SOURCES += icons/open/view_48x.h
vmware_view_SOURCES += icons/open/view_client_banner.h
vmware_view_SOURCES += icons/list_button_normal.h
vmware_view_SOURCES += icons/list_button_hover.h
vmware_view_SOURCES += icons/list_button_open.h

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
vmware_view_CPPFLAGS += $(GTK_CFLAGS)
vmware_view_CPPFLAGS += $(ICU_CPPFLAGS)
vmware_view_CPPFLAGS += $(SSL_CFLAGS)
vmware_view_CPPFLAGS += $(XML_CFLAGS)

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
vmware_view_LDADD += libPollGtk.a
vmware_view_LDADD += libGtm.a
vmware_view_LDADD += libProductState.a
vmware_view_LDADD += libSig.a
vmware_view_LDADD += libSsl.a
vmware_view_LDADD += libDict.a
vmware_view_LDADD += libMisc.a
vmware_view_LDADD += libErr.a
vmware_view_LDADD += libUnicode.a
vmware_view_LDADD += $(GTK_LIBS)
vmware_view_LDADD += $(XML_LIBS)
vmware_view_LDADD += $(CURL_LIBS)
vmware_view_LDADD += $(SSL_LIBS)
vmware_view_LDADD += $(BOOST_LDFLAGS) $(BOOST_SIGNALS_LIB)
vmware_view_LDADD += $(ICU_LIBS)

dist_noinst_DATA += icons/open/view_48x.png

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
	cp $^ $@

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
	mv $(DEB_STAGE_ROOT).deb $(PACKAGING_NAME)_$(VERSION)_$(RPM_ARCH).deb
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

TAR_STAGE_DIR := $(PACKAGING_NAME)-$(VERSION)

# Make the locale stuff in the tar stage dir. Note the .. since we're entering
# po/ and want the stage dir to be in the directory above it.
bindist: all-am
	rm -rf $(TAR_STAGE_DIR)
	$(MKDIR_P) $(TAR_STAGE_DIR)/bin
	$(MKDIR_P) $(TAR_STAGE_DIR)/doc/help
	STRIPPROG='$(STRIP)' $(INSTALL_STRIP_PROGRAM) vmware-view \
		$(TAR_STAGE_DIR)/bin
	STRIPPROG='$(STRIP)' $(INSTALL_STRIP_PROGRAM) vmware-view-tunnel \
		$(TAR_STAGE_DIR)/bin
	$(INSTALL_DATA) README.txt $(TAR_STAGE_DIR)
	$(INSTALL_DATA) doc/open_source_licenses.txt \
		$(TAR_STAGE_DIR)/doc
	$(INSTALL_DATA) $(srcdir)/doc/View_Client_Admin_Guide-*.pdf \
		$(TAR_STAGE_DIR)/doc
if VIEW_EULAS
	$(INSTALL_DATA) $(srcdir)/doc/$(PACKAGING_NAME)-EULA-*.txt \
		$(TAR_STAGE_DIR)/doc || \
	$(INSTALL_DATA) $(builddir)/doc/$(PACKAGING_NAME)-EULA-*.txt \
		$(TAR_STAGE_DIR)/doc
endif
	$(INSTALL_SCRIPT) $(srcdir)/vmware-view-log-collector \
		$(TAR_STAGE_DIR)/bin
	$(INSTALL_DATA) $(srcdir)/doc/help/integrated_help-*.txt \
		$(TAR_STAGE_DIR)/doc/help
	$(MAKE) -C po $(AM_MAKEFLAGS) install itlocaledir=../$(TAR_STAGE_DIR)/share/locale
	tardir=$(TAR_STAGE_DIR) && $(am__tar) | GZIP=$(GZIP_ENV) $(GZIP) -c \
		>$(TAR_STAGE_DIR).tar.gz
	-rm -rf $(TAR_STAGE_DIR)

ifneq ($(RPMBUILD),)
deliverables: rpms
@ENDIF@
ifneq ($(DPKG_DEB),)
deliverables: debs
@ENDIF@
ifeq ($(PACKAGING_NAME),VMware-view-open-client)
# XXX: dist should be distcheck
deliverables: dist
@ENDIF@
deliverables: bindist
ifneq ($(PUBLISH_DIR),)
	$(MKDIR_P) $(PUBLISH_DIR)
	$(INSTALL_DATA) $(TAR_STAGE_DIR).tar.gz $(PUBLISH_DIR)
ifeq ($(PACKAGING_NAME),VMware-view-open-client)
	$(INSTALL_DATA) $(distdir).tar.gz $(PUBLISH_DIR)
@ENDIF@ # PACKAGING_NAME
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
	$(INSTALL_DATA) $(PACKAGING_NAME)_$(VERSION)_$(RPM_ARCH).deb \
		$(PUBLISH_DIR)
@ENDIF@ # DPKG_DEB
@ENDIF@ # PUBLISH_DIR
