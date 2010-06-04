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
 * cdkProxy.h --
 *
 *      A simple interface for getting the proxy settings for a given
 *      URL.
 */

#ifndef CDK_PROXY_H
#define CDK_PROXY_H


#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
   CDK_PROXY_NONE,
   CDK_PROXY_HTTP,
   CDK_PROXY_SOCKS4
} CdkProxyType;


/* Don't forget to free the returned string with g_free(). */
char *CdkProxy_GetProxyForUrl(const char *aUrl, CdkProxyType *proxyType);

#ifdef __APPLE__
char *CdkProxy_CFStringToUTF8CString(CFStringRef s);
#endif


#ifdef __cplusplus
}
#endif


#endif /* CDK_PROXY_H */
