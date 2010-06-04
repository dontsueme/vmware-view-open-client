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

bin_PROGRAMS += vmware-view-tunnel

vmware_view_tunnel_SOURCES :=
if VIEW_COCOA
vmware_view_tunnel_OBJCFLAGS =
# Automake does not support .mm files, so we need to turn
# Objective-C++ on manually.
vmware_view_tunnel_OBJCFLAGS += -x objective-c++
vmware_view_tunnel_SOURCES += tunnel/tunnelCocoa.m
else
vmware_view_tunnel_SOURCES += tunnel/tunnel.cc
endif
vmware_view_tunnel_SOURCES += tunnel/tunnelMain.c
vmware_view_tunnel_SOURCES += tunnel/tunnelProxy.c
vmware_view_tunnel_SOURCES += tunnel/tunnelProxy.h
vmware_view_tunnel_SOURCES += lib/open-vm-tools/misc/base64.c
nodist_vmware_view_tunnel_SOURCES :=
if STATIC_ICU
nodist_vmware_view_tunnel_SOURCES += $(ICUDATA_ENTRY_POINT)l_dat.s
endif

vmware_view_tunnel_CPPFLAGS := $(AM_CPPFLAGS) $(SSL_CFLAGS)

vmware_view_tunnel_LDADD :=
vmware_view_tunnel_LDADD += libCdk.a
vmware_view_tunnel_LDADD += $(ICU_LIBS)
vmware_view_tunnel_LDADD += $(SSL_LIBS)
vmware_view_tunnel_LDADD += libPoll.a
vmware_view_tunnel_LDADD += libPollDefault.a
if VIEW_GTK
vmware_view_tunnel_LDADD += libPollGtk.a
endif
vmware_view_tunnel_LDADD += $(GLIB_LIBS)
if VIEW_COCOA
vmware_view_tunnel_LDADD += libDui.a
vmware_view_tunnel_LDFLAGS = -framework Foundation
endif
if VIEW_WIN32
vmware_view_tunnel_LDADD += -lws2_32
endif
vmware_view_tunnel_LDADD += $(TUNNEL_FRAMEWORKS)
