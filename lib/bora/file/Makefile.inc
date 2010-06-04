# -*- Makefile -*-
################################################################################
# Copyright 2008-2010 VMware, Inc.  All rights reserved.
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

if VIEW_WIN32
libFile_a_SOURCES += lib/bora/file/fileIOWin32.c
libFile_a_SOURCES += lib/bora/file/fileLockWin32.c
libFile_a_SOURCES += lib/bora/file/fileWin32.c
libFile_a_CPPFLAGS += -I$(top_srcdir)/lib/open-vm-tools/file
endif # VIEW_WIN32
