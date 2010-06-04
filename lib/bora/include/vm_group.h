/*********************************************************
 * Copyright (C) 1998-2010 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is released with an additional exemption that
 * compiling, linking, and/or using the OpenSSL libraries with this
 * program is allowed.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

#include "vm_version.h"

/*
 * Unicode string macros.
 */
#define __UC(x)      L ## x
#define _UC(x)       __UC(x)

/*
 * The names of the VMware group and user that we create for security purposes.
 */
#define VMWARE_USER        L"__vmware_user__"
#define VMWARE_USER_CSTR   "__vmware_user__"
#define VMWARE_USER_DESC   L"VMware User"
#define VMWARE_GROUP	   L"__vmware__"
#define VMWARE_GROUP_DESC  L"VMware User Group"

/*
 * The registry key used to save the VMware group SID for use by the driver.
 */
#ifdef VM_X86_64
#define VMWARE_SID_REGKEY       L"SOFTWARE\\Wow6432Node\\" _UC(COMPANY_NAME) L"\\SID"
/*
 * The registry key used to store username/password for VMs.  I'd like this to be
 * a wide-char string, but _UC(PRODUCT_REG_NAME) doesn't work since PRODUCT_REG_NAME
 * is itself a concatentation of 8-bit chars (which causes a width mismatch).
 */
#define VMWARE_ACCTINFO_REGKEY  "SOFTWARE\\Wow6432Node\\" COMPANY_NAME "\\" \
			        PRODUCT_REG_NAME "\\AccountInfo"
#else
#define VMWARE_SID_REGKEY       L"SOFTWARE\\" _UC(COMPANY_NAME) L"\\SID"
#define VMWARE_ACCTINFO_REGKEY  "SOFTWARE\\" COMPANY_NAME "\\" \
			        PRODUCT_REG_NAME "\\AccountInfo"
#endif
