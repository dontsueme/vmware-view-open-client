/*********************************************************
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
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

/*
 * cdkDebugAssert.h --
 *
 *      Function for using OS X AssertMacros with Cdk.
 */

#ifndef CDK_DEBUG_ASSERT_H
#define CDK_DEBUG_ASSERT_H

#define DEBUG_ASSERT_COMPONENT_NAME_STRING PRODUCT_VIEW_CLIENT_NAME

#define DEBUG_ASSERT_MESSAGE(componentNameString, assertionString,      \
                             exceptionLabelString, errorString, fileName, lineNumber, errorCode) \
    CdkDebugAssert(componentNameString, assertionString,                \
                   exceptionLabelString, errorString, fileName, lineNumber, errorCode)

#ifdef __cplusplus
extern "C"
#endif
void CdkDebugAssert(const char *componentNameString, const char *assertionString,
                    const char *exceptionLabelString, const char *errorString,
                    const char *fileName, long lineNumber, int errorCode);

#endif CDK_DEBUG_ASSERT_H

/*
 * The above needs to be defined *before* AssertMacros.h is included.
 */

#include <AssertMacros.h>
