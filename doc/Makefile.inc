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

if VIEW_EULAS
doc_DATA += $(foreach lang,en $(ALL_LINGUAS),doc/$(PACKAGING_NAME)-EULA-$(lang).rtf)
doc_DATA += $(foreach lang,en $(ALL_LINGUAS),doc/$(PACKAGING_NAME)-EULA-$(lang).txt)
endif

dist_doc_DATA += doc/open_source_licenses.txt

man_MANS += doc/vmware-view.1
man_MANS += doc/vmware-view-tunnel.1

dist_noinst_DATA += doc/copyright.Debian

dist_helpde_DATA += doc/help/de/connect.txt
dist_helpde_DATA += doc/help/de/list.txt
dist_helpde_DATA += doc/help/de/login.txt

dist_helpen_DATA += doc/help/en/connect.txt
dist_helpen_DATA += doc/help/en/list.txt
dist_helpen_DATA += doc/help/en/login.txt

dist_helpfr_DATA += doc/help/fr/connect.txt
dist_helpfr_DATA += doc/help/fr/list.txt
dist_helpfr_DATA += doc/help/fr/login.txt

dist_helpja_DATA += doc/help/ja/connect.txt
dist_helpja_DATA += doc/help/ja/list.txt
dist_helpja_DATA += doc/help/ja/login.txt

dist_helpzh_CN_DATA += doc/help/zh_CN/connect.txt
dist_helpzh_CN_DATA += doc/help/zh_CN/list.txt
dist_helpzh_CN_DATA += doc/help/zh_CN/login.txt
