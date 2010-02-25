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

dist_pdf_DATA += doc/View_Client_Admin_Guide-en.pdf
dist_pdf_DATA += doc/View_Client_Admin_Guide-de.pdf
dist_pdf_DATA += doc/View_Client_Admin_Guide-fr.pdf
dist_pdf_DATA += doc/View_Client_Admin_Guide-ja.pdf
dist_pdf_DATA += doc/View_Client_Admin_Guide-zh_CN.pdf

if VIEW_EULAS
doc_DATA += doc/$(PACKAGING_NAME)-EULA-en.txt
doc_DATA += doc/$(PACKAGING_NAME)-EULA-de.txt
doc_DATA += doc/$(PACKAGING_NAME)-EULA-fr.txt
doc_DATA += doc/$(PACKAGING_NAME)-EULA-ja.txt
doc_DATA += doc/$(PACKAGING_NAME)-EULA-zh_CN.txt
endif

dist_doc_DATA += doc/open_source_licenses.txt

man_MANS += doc/vmware-view.1
man_MANS += doc/vmware-view-tunnel.1

dist_noinst_DATA += doc/copyright.Debian

dist_help_DATA += doc/help/integrated_help-en.txt
dist_help_DATA += doc/help/integrated_help-de.txt
dist_help_DATA += doc/help/integrated_help-fr.txt
dist_help_DATA += doc/help/integrated_help-ja.txt
dist_help_DATA += doc/help/integrated_help-zh_CN.txt
