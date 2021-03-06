# -*- Makefile -*-
################################################################################
### Copyright 2007 VMware, Inc.  All rights reserved.
###
### This program is free software; you can redistribute it and/or modify
### it under the terms of version 2 of the GNU General Public License as
### published by the Free Software Foundation.
###
### This program is distributed in the hope that it will be useful,
### but WITHOUT ANY WARRANTY; without even the implied warranty of
### MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
### GNU General Public License for more details.
###
### You should have received a copy of the GNU General Public License
### along with this program; if not, write to the Free Software
### Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
################################################################################

noinst_LIBRARIES += libString.a

libString_a_SOURCES =
libString_a_SOURCES += lib/open-vm-tools/string/bsd_output_int.h
libString_a_SOURCES += lib/open-vm-tools/string/bsd_output_shared.c
libString_a_SOURCES += lib/open-vm-tools/string/bsd_vsnprintf.c
libString_a_SOURCES += lib/open-vm-tools/string/convertutf.c
libString_a_SOURCES += lib/open-vm-tools/string/convertutf.h
libString_a_SOURCES += lib/open-vm-tools/string/str.c

libString_a_CFLAGS = @LIB_STRING_CPPFLAGS@
