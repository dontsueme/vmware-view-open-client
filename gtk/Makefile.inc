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

if VIEW_GTK
vmware_view_SOURCES += gtk/app.cc
vmware_view_SOURCES += gtk/app.hh
vmware_view_SOURCES += gtk/brokerDlg.cc
vmware_view_SOURCES += gtk/brokerDlg.hh
vmware_view_SOURCES += gtk/certViewer.hh
vmware_view_SOURCES += gtk/cryptoki.cc
vmware_view_SOURCES += gtk/cryptoki.hh
if !VIEW_WIN32
vmware_view_SOURCES += gtk/desktopDlg.cc
vmware_view_SOURCES += gtk/desktopDlg.hh
endif # !VIEW_WIN32
vmware_view_SOURCES += gtk/desktopSelectDlg.cc
vmware_view_SOURCES += gtk/desktopSelectDlg.hh
vmware_view_SOURCES += gtk/disclaimerDlg.cc
vmware_view_SOURCES += gtk/disclaimerDlg.hh
vmware_view_SOURCES += gtk/dlg.cc
vmware_view_SOURCES += gtk/dlg.hh
vmware_view_SOURCES += gtk/helpSupportDlg.cc
vmware_view_SOURCES += gtk/helpSupportDlg.hh
vmware_view_SOURCES += gtk/kioskWindow.cc
vmware_view_SOURCES += gtk/kioskWindow.hh
vmware_view_SOURCES += gtk/loginDlg.cc
vmware_view_SOURCES += gtk/loginDlg.hh
if !VIEW_CVP
vmware_view_SOURCES += gtk/main.cc
endif # !VIEW_CVP
if VIEW_WIN32
vmware_view_SOURCES += gtk/mstsc.cc
vmware_view_SOURCES += gtk/mstsc.hh
endif # VIEW_WIN32
vmware_view_SOURCES += gtk/passwordDlg.cc
vmware_view_SOURCES += gtk/passwordDlg.hh
vmware_view_SOURCES += gtk/prefs.cc
vmware_view_SOURCES += gtk/prefs.hh
if !VIEW_WIN32
vmware_view_SOURCES += gtk/rdesktop.cc
vmware_view_SOURCES += gtk/rdesktop.hh
vmware_view_SOURCES += gtk/rmks.cc
vmware_view_SOURCES += gtk/rmks.hh
endif # !VIEW_WIN32
vmware_view_SOURCES += gtk/scCertDetailsDlg.cc
vmware_view_SOURCES += gtk/scCertDetailsDlg.hh
vmware_view_SOURCES += gtk/scCertDlg.cc
vmware_view_SOURCES += gtk/scCertDlg.hh
vmware_view_SOURCES += gtk/scInsertPromptDlg.cc
vmware_view_SOURCES += gtk/scInsertPromptDlg.hh
vmware_view_SOURCES += gtk/scPinDlg.cc
vmware_view_SOURCES += gtk/scPinDlg.hh
vmware_view_SOURCES += gtk/securIDDlg.cc
vmware_view_SOURCES += gtk/securIDDlg.hh
vmware_view_SOURCES += gtk/transitionDlg.cc
vmware_view_SOURCES += gtk/transitionDlg.hh
vmware_view_SOURCES += gtk/window.cc
vmware_view_SOURCES += gtk/window.hh
vmware_view_SOURCES += gtk/windowSizeDlg.cc
vmware_view_SOURCES += gtk/windowSizeDlg.hh
endif # VIEW_GTK
